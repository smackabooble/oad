#include "js/jsapi.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace JS {
const Value UndefinedHandleValueStorage;
const Value NullHandleValueStorage(JS_NULL);
const Value TrueHandleValueStorage(JS_TRUE);
const Value FalseHandleValueStorage(JS_FALSE);
}

namespace {
thread_local JSShimCtx t_ctx;
thread_local JSContext* t_globalCx = nullptr;
thread_local JSRuntime* t_globalRt = nullptr;
using JS::Value;

inline size_t JSShim_StrLen(JSValue s) {
	JSValue l = JS_GetPropertyStr(t_globalCx, s, "length"); uint32_t n = 0;
	JS_ToUint32(t_globalCx, &n, l); JS_FreeValue(t_globalCx, l); return n;
}
inline JSValue V(const Value& v) { return v.raw(); }
inline JSValue VO(JSObject* o) { return JS::Conv<JSObject*>::to(o); }
inline Value Pooled(JSContext* cx, JSValue v) {
	if (JS_VALUE_HAS_REF_COUNT(v)) JSShim_Pool(cx, v);
	return Value(v);
}
inline JSObject* PooledObj(JSContext* cx, JSValue v) {
	if (JS_IsException(v)) return nullptr;
	Value r = Pooled(cx, v);
	return r.toObjectOrNull();
}
struct Atom {
	JSContext* cx; JSAtom a;
	Atom(JSContext* c, const char* s) : cx(c), a(JS_NewAtom(c, s)) {}
	Atom(JSContext* c, const char16_t* s, size_t n) : cx(c) {
		JSValue str = JS_NewTwoByteString(c, (const uint16_t*)s, n);
		a = JS_ValueToAtom(c, str); JS_FreeValue(c, str);
	}
	~Atom() { JS_FreeAtom(cx, a); }
};
unsigned PropFlags(unsigned attrs) {
	unsigned f = JS_PROP_CONFIGURABLE | JS_PROP_HAS_CONFIGURABLE | JS_PROP_HAS_WRITABLE | JS_PROP_HAS_ENUMERABLE | JS_PROP_HAS_VALUE;
	if (!(attrs & JSPROP_READONLY)) f |= JS_PROP_WRITABLE;
	if (attrs & JSPROP_ENUMERATE) f |= JS_PROP_ENUMERABLE;
	if (attrs & JSPROP_PERMANENT) f &= ~JS_PROP_CONFIGURABLE;
	return f;
}
bool DefProp(JSContext* cx, JSObject* obj, JSAtom a, JSValue v, unsigned attrs) {
	JS_DupValue(cx, v);
	return JS_DefineProperty(cx, VO(obj), a, v, JS_UNDEFINED, JS_UNDEFINED, PropFlags(attrs)) >= 0;
}

JSValue Trampoline(JSContext* cx, JSValueConst thisv, int argc, JSValueConst* argv, int, JSValue* data) {
	JSNative fn = (JSNative)(intptr_t)JS_VALUE_GET_INT(data[0]);
	// Natives run in the context (realm) that created them, as in SpiderMonkey: the callback data of the
	// callee's interface must be seen even when a script of another interface (e.g. the AI) calls them.
	cx = (JSContext*)(intptr_t)JS_VALUE_GET_INT(data[1]);
	std::vector<Value> frame(argc + 2);
	frame[0] = Value();
	frame[1] = Value(thisv);
	for (int i = 0; i < argc; i++) frame[i + 2] = Value(argv[i]);
	if (!fn(cx, (unsigned)argc, frame.data())) {
		if (!JS_HasException(cx)) JS_ThrowInternalError(cx, "native function failed");
		return JS_EXCEPTION;
	}
	return JS_DupValue(cx, frame[0].raw());
}
JSValue NewNative(JSContext* cx, JSNative call, unsigned nargs, const char* name) {
	JSValue data[2] = { JS_MKVAL(JS_TAG_INT, (int32_t)(intptr_t)call), JS_MKVAL(JS_TAG_INT, (int32_t)(intptr_t)cx) };
	JSValue f = JS_NewCFunctionData(cx, Trampoline, (int)nargs, 0, 2, data);
	if (name) { JSValue n = JS_NewString(cx, name); JS_DefinePropertyValueStr(cx, f, "name", n, JS_PROP_CONFIGURABLE); }
	return f;
}
} // namespace

JSShimCtx& JSShim_Ctx(JSContext*) { return t_ctx; }
namespace { void ClearStrKeep(); }
namespace { thread_local std::vector<JS::PersistentSyncBase*> t_dirty; }
void JS::JSShim_RegisterDirty(JS::PersistentSyncBase* p) { p->registered_ = true; t_dirty.push_back(p); }
void JS::JSShim_UnregisterDirty(JS::PersistentSyncBase* p) {
	p->registered_ = false; for (size_t i = 0; i < t_dirty.size(); i++) if (t_dirty[i] == p) { t_dirty.erase(t_dirty.begin() + i); break; }
}
namespace { thread_local unsigned t_dirtyGen = 0; }
unsigned JS::JSShim_DirtyGen() { return t_dirtyGen; }
void JSShim_PoolRelease(JSContext* cx, size_t keep) {
	// Scratch values written through `&persistent` are made real references here. An entry stays registered for one
	// more release (grace) because the write may come just after this release ran.
	std::vector<JS::PersistentSyncBase*> kept;
	for (auto* p : t_dirty) {
		p->sync();
		if (p->gen_ < t_dirtyGen) { p->retire(); p->registered_ = false; } else kept.push_back(p);
	}
	t_dirty.swap(kept);
	t_dirtyGen++;
	ClearStrKeep();
	auto& p = t_ctx.pool;
	while (p.size() > keep) { JS_FreeValue(cx, p.back()); p.pop_back(); }
}
JSRuntime* JS::JSShim_GlobalRt() { return t_globalRt; }

void JSShim_SetGlobalCx(JSContext* cx) { t_globalCx = cx; t_globalRt = cx ? JS_GetRuntime(cx) : nullptr; }

// ---- objects ----
JSObject* JS_NewPlainObject(JSContext* cx) { return PooledObj(cx, JS_NewObject(cx)); }
JSObject* JS_DefineObject(JSContext* cx, JS::HandleObject obj, const char* name, const JSClass*, unsigned attrs) {
	JSObject* o = JS_NewPlainObject(cx);
	Atom a(cx, name); DefProp(cx, obj.get(), a.a, VO(o), attrs);
	return o;
}
bool JS_SetProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleValue v) {
	return JS_SetPropertyStr(cx, VO(obj), name, JS_DupValue(cx, V(v))) >= 0;
}
bool JS_GetProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::MutableHandleValue vp) {
	JSValue r = JS_GetPropertyStr(cx, VO(obj), name);
	if (JS_IsException(r)) return false;
	vp.set(Pooled(cx, r)); return true;
}
bool JS_HasProperty(JSContext* cx, JS::HandleObject obj, const char* name, bool* found) {
	Atom a(cx, name); int r = JS_HasProperty(cx, VO(obj), a.a);
	if (r < 0) return false; *found = r; return true;
}
bool JS_SetUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* n, size_t l, JS::HandleValue v) {
	Atom a(cx, n, l); return JS_SetProperty(cx, VO(obj), a.a, JS_DupValue(cx, V(v))) >= 0;
}
bool JS_GetUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* n, size_t l, JS::MutableHandleValue vp) {
	Atom a(cx, n, l); JSValue r = JS_GetProperty(cx, VO(obj), a.a);
	if (JS_IsException(r)) return false; vp.set(Pooled(cx, r)); return true;
}
bool JS_HasUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* n, size_t l, bool* found) {
	Atom a(cx, n, l); int r = JS_HasProperty(cx, VO(obj), a.a);
	if (r < 0) return false; *found = r; return true;
}
bool JS_SetElement(JSContext* cx, JS::HandleObject obj, uint32_t i, JS::HandleValue v) {
	return JS_SetPropertyUint32(cx, VO(obj), i, JS_DupValue(cx, V(v))) >= 0;
}
bool JS_GetElement(JSContext* cx, JS::HandleObject obj, uint32_t i, JS::MutableHandleValue vp) {
	JSValue r = JS_GetPropertyUint32(cx, VO(obj), i);
	if (JS_IsException(r)) return false; vp.set(Pooled(cx, r)); return true;
}
bool JS_HasElement(JSContext* cx, JS::HandleObject obj, uint32_t i, bool* found) {
	JSAtom a = JS_NewAtomUInt32(cx, i); int r = JS_HasProperty(cx, VO(obj), a); JS_FreeAtom(cx, a);
	if (r < 0) return false; *found = r; return true;
}
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleValue v, unsigned attrs) {
	Atom a(cx, name); return DefProp(cx, obj.get(), a.a, V(v), attrs);
}
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleObject v, unsigned attrs) {
	Atom a(cx, name); return DefProp(cx, obj.get(), a.a, VO(v), attrs);
}
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, int32_t v, unsigned attrs) {
	Atom a(cx, name); return DefProp(cx, obj.get(), a.a, JS_NewInt32(cx, v), attrs);
}
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, uint32_t v, unsigned attrs) {
	Atom a(cx, name); return DefProp(cx, obj.get(), a.a, JS_NewUint32(cx, v), attrs);
}
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, double v, unsigned attrs) {
	Atom a(cx, name); return DefProp(cx, obj.get(), a.a, JS::ShimNum(v), attrs);
}
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, bool v, unsigned attrs) {
	Atom a(cx, name); return DefProp(cx, obj.get(), a.a, JS_NewBool(cx, v), attrs);
}
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleString v, unsigned attrs) {
	Atom a(cx, name); return DefProp(cx, obj.get(), a.a, JS::Conv<JSString*>::to(v), attrs);
}
bool JS_DefinePropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, JS::HandleValue v, unsigned attrs) {
	return DefProp(cx, obj.get(), id->atom, V(v), attrs);
}
bool JS_DefineUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* n, size_t l, JS::HandleValue v, unsigned attrs) {
	Atom a(cx, n, l); return DefProp(cx, obj.get(), a.a, V(v), attrs);
}
bool JS_DefineProperties(JSContext* cx, JS::HandleObject obj, const JSPropertySpec* ps) {
	for (; ps && ps->name; ++ps) {
		Atom a(cx, ps->name);
		JSValue g = ps->getter ? NewNative(cx, ps->getter, 0, ps->name) : JS_UNDEFINED;
		JSValue s = ps->setter ? NewNative(cx, ps->setter, 1, ps->name) : JS_UNDEFINED;
		unsigned f = JS_PROP_HAS_GET | JS_PROP_HAS_SET | JS_PROP_HAS_CONFIGURABLE | JS_PROP_HAS_ENUMERABLE;
		if (ps->flags & JSPROP_ENUMERATE) f |= JS_PROP_ENUMERABLE;
		if (!(ps->flags & JSPROP_PERMANENT)) f |= JS_PROP_CONFIGURABLE;
		int r = JS_DefineProperty(cx, VO(obj), a.a, JS_UNDEFINED, g, s, f);
		JS_FreeValue(cx, g); JS_FreeValue(cx, s);
		if (r < 0) return false;
	}
	return true;
}
bool JS_DeleteProperty(JSContext* cx, JS::HandleObject obj, const char* name) {
	Atom a(cx, name); return JS_DeleteProperty(cx, VO(obj), a.a, 0) >= 0;
}
bool JS_DeleteProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::ObjectOpResult& r) {
	Atom a(cx, name); int x = JS_DeleteProperty(cx, VO(obj), a.a, 0);
	if (x < 0) return false; if (!x) r.fail(0); return true;
}
bool JS_GetPropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, JS::MutableHandleValue vp) {
	JSValue r = JS_GetProperty(cx, VO(obj), id->atom);
	if (JS_IsException(r)) return false; vp.set(Pooled(cx, r)); return true;
}
bool JS_SetPropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, JS::HandleValue v) {
	return JS_SetProperty(cx, VO(obj), id->atom, JS_DupValue(cx, V(v))) >= 0;
}
bool JS_HasPropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, bool* found) {
	int r = JS_HasProperty(cx, VO(obj), id->atom); if (r < 0) return false; *found = r; return true;
}
bool JS_IdToValue(JSContext* cx, JS::PropertyKey id, JS::MutableHandleValue vp) {
	vp.set(Pooled(cx, JS_AtomToValue(cx, id.atom))); return true;
}
bool JS_ValueToId(JSContext* cx, JS::HandleValue v, JS::MutableHandleId idp) {
	JSAtom a = JS_ValueToAtom(cx, V(v)); if (a == JS_ATOM_NULL) return false;
	idp.set(JS::PropertyKey(a)); return true; // atom intentionally leaked (interned, small)
}
bool JS_GetPrototype(JSContext* cx, JS::HandleObject obj, JS::MutableHandleObject result) {
	JSValue p = JS_GetPrototype(cx, VO(obj)); result.set(PooledObj(cx, p)); return true;
}
bool JS_SetPrototype(JSContext* cx, JS::HandleObject obj, JS::HandleObject proto) {
	return JS_SetPrototype(cx, VO(obj), proto.get() ? VO(proto) : JS_NULL) >= 0;
}
bool JS_FreezeObject(JSContext* cx, JS::HandleObject obj) {
	JSValue o = VO(obj);
	JSValue ObjC = JS_GetPropertyStr(cx, JS_GetGlobalObject(cx), "Object");
	JSValue fr = JS_GetPropertyStr(cx, ObjC, "freeze");
	JSValue r = JS_Call(cx, fr, ObjC, 1, &o);
	JS_FreeValue(cx, r); JS_FreeValue(cx, fr); JS_FreeValue(cx, ObjC);
	return true;
}
bool JS_DeepFreezeObject(JSContext* cx, JS::HandleObject obj) { return JS_FreezeObject(cx, obj); }

// ---- functions ----
JSFunction* JS_DefineFunction(JSContext* cx, JS::HandleObject obj, const char* name, JSNative call, unsigned nargs, unsigned attrs) {
	JSValue f = NewNative(cx, call, nargs, name);
	Atom a(cx, name);
	bool ok = DefProp(cx, obj.get(), a.a, f, attrs);
	JSObject* fo = PooledObj(cx, f);  // takes over our reference
	return ok ? fo : nullptr;
}
bool JS_DefineFunctions(JSContext* cx, JS::HandleObject obj, const JSFunctionSpec* fs) {
	for (; fs && fs->name; ++fs)
		if (!JS_DefineFunction(cx, obj, fs->name, fs->call, fs->nargs, fs->flags)) return false;
	return true;
}
JSFunction* JS_NewFunction(JSContext* cx, JSNative call, unsigned nargs, unsigned, const char* name) {
	return PooledObj(cx, NewNative(cx, call, nargs, name));
}
bool JS_CallFunctionValue(JSContext* cx, JS::HandleObject obj, JS::HandleValue fval, const JS::HandleValueArray& args, JS::MutableHandleValue rval) {
	std::vector<JSValue> a; for (size_t i = 0; i < args.length(); i++) a.push_back(args.begin()[i].raw());
	JSValue r = JS_Call(cx, V(fval), obj.get() ? VO(obj) : JS_UNDEFINED, (int)a.size(), a.data());
	if (JS_IsException(r)) return false; rval.set(Pooled(cx, r)); return true;
}
bool JS_CallFunctionName(JSContext* cx, JS::HandleObject obj, const char* name, const JS::HandleValueArray& args, JS::MutableHandleValue rval) {
	JSValue f = JS_GetPropertyStr(cx, VO(obj), name);
	if (JS_IsException(f)) return false;
	JS::Value fv = Pooled(cx, f);
	return JS_CallFunctionValue(cx, obj, JS::HandleValue(fv), args, rval);
}
namespace JS {
bool Call(JSContext* cx, HandleValue thisv, HandleValue fun, const HandleValueArray& args, MutableHandleValue rval) {
	std::vector<JSValue> a; for (size_t i = 0; i < args.length(); i++) a.push_back(args.begin()[i].raw());
	JSValue r = JS_Call(cx, V(fun), V(thisv), (int)a.size(), a.data());
	if (JS_IsException(r)) return false; rval.set(Pooled(cx, r)); return true;
}
bool Call(JSContext* cx, HandleObject thisv, HandleValue fun, const HandleValueArray& args, MutableHandleValue rval) {
	Value t = ObjectOrNullValue(thisv.get()); return Call(cx, HandleValue(t), fun, args, rval);
}
bool Construct(JSContext* cx, HandleValue fun, const HandleValueArray& args, MutableHandleObject obj) {
	std::vector<JSValue> a; for (size_t i = 0; i < args.length(); i++) a.push_back(args.begin()[i].raw());
	JSValue r = JS_CallConstructor(cx, V(fun), (int)a.size(), a.data());
	if (JS_IsException(r)) return false; obj.set(PooledObj(cx, r)); return true;
}
JSObject* NewArrayObject(JSContext* cx, size_t length) {
	JSValue a = JS_NewArray(cx);
	if (length) JS_SetPropertyStr(cx, a, "length", JS_NewUint32(cx, (uint32_t)length));
	return PooledObj(cx, a);
}
JSObject* NewArrayObject(JSContext* cx, const HandleValueArray& c) {
	JSValue a = JS_NewArray(cx);
	for (size_t i = 0; i < c.length(); i++) JS_SetPropertyUint32(cx, a, (uint32_t)i, JS_DupValue(cx, c.begin()[i].raw()));
	return PooledObj(cx, a);
}
bool GetArrayLength(JSContext* cx, HandleObject obj, uint32_t* out) {
	JSValue l = JS_GetPropertyStr(cx, VO(obj), "length"); if (JS_IsException(l)) return false;
	bool ok = JS_ToUint32(cx, out, l) >= 0; JS_FreeValue(cx, l); return ok;
}
bool IsArrayObject(JSContext* cx, HandleObject obj, bool* r) { *r = obj.get() && JS_IsArray(VO(obj)); return true; }
bool IsArrayObject(JSContext* cx, HandleValue v, bool* r) { *r = JS_IsArray(V(v)); return true; }
JSString* ToString(JSContext* cx, HandleValue v) {
	JSValue s = JS_ToString(cx, V(v)); if (JS_IsException(s)) return nullptr;
	Value p = Pooled(cx, s); return Conv<JSString*>::from(p.raw());
}
bool ToNumber(JSContext* cx, HandleValue v, double* out) { return JS_ToFloat64(cx, out, V(v)) >= 0; }
bool ToInt32(JSContext* cx, HandleValue v, int32_t* out) { return JS_ToInt32(cx, out, V(v)) >= 0; }
bool ToUint32(JSContext* cx, HandleValue v, uint32_t* out) { return JS_ToUint32(cx, out, V(v)) >= 0; }
bool ToUint16(JSContext* cx, HandleValue v, uint16_t* out) { uint32_t u; if (JS_ToUint32(cx, &u, V(v)) < 0) return false; *out = (uint16_t)u; return true; }
bool ToInt64(JSContext* cx, HandleValue v, int64_t* out) { return JS_ToInt64(cx, out, V(v)) >= 0; }
bool ToBoolean(HandleValue v) { return JS_ToBool(t_globalCx, V(v)) > 0; }
bool CallArgs::requireAtLeast(JSContext* cx, const char* fn, unsigned required) const {
	if (argc_ >= required) return true;
	JS_ThrowTypeError(cx, "%s requires at least %u argument(s)", fn, required); return false;
}
} // namespace JS
bool JS_ObjectIsFunction(JSObject* obj) { return obj && JS_IsFunction(t_globalCx, VO(obj)); }

// ---- strings ----
JSString* JS_NewStringCopyN(JSContext* cx, const char* s, size_t n) {
	Value v = Pooled(cx, JS_NewStringLen(cx, s, n)); return JS::Conv<JSString*>::from(v.raw());
}
JSString* JS_NewStringCopyZ(JSContext* cx, const char* s) { return JS_NewStringCopyN(cx, s, strlen(s)); }
JSString* JS_NewUCStringCopyN(JSContext* cx, const char16_t* s, size_t n) {
	Value v = Pooled(cx, JS_NewTwoByteString(cx, (const uint16_t*)s, n)); return JS::Conv<JSString*>::from(v.raw());
}
JSString* JS_NewUCStringCopyZ(JSContext* cx, const char16_t* s) {
	size_t n = 0; while (s[n]) n++; return JS_NewUCStringCopyN(cx, s, n);
}
JSString* JS_ValueToSource(JSContext* cx, JS::HandleValue v) { return JS::ToString(cx, v); }
size_t JS_GetStringLength(JSString* str) {
	return str ? (size_t)JSShim_StrLen(JS::Conv<JSString*>::to(str)) : 0;
}
std::string JSShim_StringToUTF8(JSContext* cx, JSString* str) {
	size_t len; const char* c = JS_ToCStringLen(cx, &len, JS::Conv<JSString*>::to(str));
	std::string r(c ? c : "", c ? len : 0); if (c) JS_FreeCString(cx, c); return r;
}
std::u16string JSShim_StringToUTF16(JSContext* cx, JSString* str) {
	std::string u = JSShim_StringToUTF8(cx, str);
	std::u16string out; out.reserve(u.size());
	for (size_t i = 0; i < u.size();) {
		unsigned char c = u[i]; uint32_t cp; int n;
		if (c < 0x80) { cp = c; n = 1; } else if (c < 0xE0) { cp = c & 0x1F; n = 2; } else if (c < 0xF0) { cp = c & 0x0F; n = 3; } else { cp = c & 0x07; n = 4; }
		for (int k = 1; k < n && i + k < u.size(); k++) cp = (cp << 6) | (u[i + k] & 0x3F);
		i += n;
		if (cp >= 0x10000) { cp -= 0x10000; out.push_back(0xD800 + (cp >> 10)); out.push_back(0xDC00 + (cp & 0x3FF)); }
		else out.push_back((char16_t)cp);
	}
	return out;
}

// ---- exceptions ----
void JS_ReportErrorUTF8(JSContext* cx, const char* fmt, ...) {
	char buf[2048]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
	JS_ThrowInternalError(cx, "%s", buf);
}
void JS_ReportErrorASCII(JSContext* cx, const char* fmt, ...) {
	char buf[2048]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
	JS_ThrowInternalError(cx, "%s", buf);
}
bool JS_IsExceptionPending(JSContext* cx) { return JS_HasException(cx); }
bool JS_GetPendingException(JSContext* cx, JS::MutableHandleValue vp) {
	if (!JS_HasException(cx)) return false;
	vp.set(Pooled(cx, JS_GetException(cx))); return true;
}
void JS_SetPendingException(JSContext* cx, JS::HandleValue v) { JS_Throw(cx, JS_DupValue(cx, V(v))); }
void JS_ClearPendingException(JSContext* cx) { JS_FreeValue(cx, JS_GetException(cx)); }
void JS_MaybeGC(JSContext*) {}
void JS_GC(JSContext* cx) { JS_RunGC(JS_GetRuntime(cx)); }

bool JS_IsTypedArrayObject(JSObject* obj) { return obj && JS_GetTypedArrayType(VO(obj)) >= 0; }
bool JS_ValueToObject(JSContext* cx, JS::HandleValue v, JS::MutableHandleObject objp) {
	if (v.isNullOrUndefined()) { objp.set(nullptr); return true; }
	if (v.isObject()) { objp.set(v.toObjectOrNull()); return true; }
	JSValue g = JS_GetGlobalObject(cx);
	JSValue Obj = JS_GetPropertyStr(cx, g, "Object");
	JSValue a = V(v.get());
	JSValue r = JS_Call(cx, Obj, JS_UNDEFINED, 1, &a);
	JS_FreeValue(cx, Obj); JS_FreeValue(cx, g);
	if (JS_IsException(r)) return false;
	objp.set(PooledObj(cx, r)); return true;
}

// ---- JSON ----
bool JS_ParseJSON(JSContext* cx, const char16_t* chars, uint32_t len, JS::MutableHandleValue vp) {
	JSValue s = JS_NewTwoByteString(cx, (const uint16_t*)chars, len);
	size_t n; const char* u = JS_ToCStringLen(cx, &n, s);
	JSValue r = JS_ParseJSON(cx, u, n, "<json>");
	JS_FreeCString(cx, u); JS_FreeValue(cx, s);
	if (JS_IsException(r)) return false;
	vp.set(Pooled(cx, r)); return true;
}
bool JS_Stringify(JSContext* cx, JS::MutableHandleValue value, JS::HandleObject replacer, JS::HandleValue space,
	JSONWriteCallback callback, void* data) {
	JSValue r = JS_JSONStringify(cx, V(value.get()), replacer.get() ? VO(replacer) : JS_UNDEFINED, V(space.get()));
	if (JS_IsException(r)) return false;
	if (JS_IsUndefined(r)) return true; // like SM: nothing written for unserialisable values
	JSString* str = JS::Conv<JSString*>::from(Pooled(cx, r).raw());
	std::u16string u = JSShim_StringToUTF16(cx, str);
	return callback(u.data(), (uint32_t)u.size(), data);
}

// ---- Property enumeration ----
const char* JS::InformalValueTypeName(const JS::Value& v) {
	if (v.isUndefined()) return "undefined"; if (v.isNull()) return "null"; if (v.isBoolean()) return "boolean";
	if (v.isNumber()) return "number"; if (v.isString()) return "string"; if (v.isSymbol()) return "symbol";
	return "object";
}
bool js::GetPropertyKeys(JSContext* cx, JS::HandleObject obj, unsigned flags, JS::RootedIdVector* props) {
	JSValue cur = JS_DupValue(cx, VO(obj));
	while (JS_IsObject(cur)) {
		JSPropertyEnum* tab; uint32_t n;
		int gpn = JS_GPN_STRING_MASK | (flags & JSITER_HIDDEN ? 0 : JS_GPN_ENUM_ONLY);
		if (JS_GetOwnPropertyNames(cx, &tab, &n, cur, gpn) < 0) { JS_FreeValue(cx, cur); return false; }
		for (uint32_t i = 0; i < n; i++) props->push_back(JS::PropertyKey(JS_DupAtom(cx, tab[i].atom)));
		JS_FreePropertyEnum(cx, tab, n);
		if (flags & JSITER_OWNONLY) break;
		JSValue p = JS_GetPrototype(cx, cur); JS_FreeValue(cx, cur); cur = p;
	}
	JS_FreeValue(cx, cur);
	return true;
}
JSObject* JS_GetConstructor(JSContext* cx, JS::HandleObject proto) {
	JSValue c = JS_GetPropertyStr(cx, VO(proto), "constructor");
	return PooledObj(cx, c);
}
uint32_t JS_GetGCParameter(JSContext* cx, JSGCParamKey key) {
	JSMemoryUsage mu; JS_ComputeMemoryUsage(JS_GetRuntime(cx), &mu);
	switch (key) { case JSGC_BYTES: return (uint32_t)mu.malloc_size; case JSGC_MAX_BYTES: return 0; default: return 0; }
}
void JS_SetGCParameter(JSContext* cx, JSGCParamKey key, uint32_t value) {
	if (key == JSGC_MAX_BYTES && value) JS_SetMemoryLimit(JS_GetRuntime(cx), value);
}

// ---- Classes ----
namespace {
std::mutex& ClassMutex();
std::map<const JSClass*, JSClassID>& ClassIds() { static std::map<const JSClass*, JSClassID> m; return m; }
std::map<JSClassID, const JSClass*>& ClassById() { static std::map<JSClassID, const JSClass*> m; return m; }
struct ProxyData { const js::BaseProxyHandler* handler; JSValue priv; JSValue slots[2]; };
std::set<JSClassID>& ProxyClasses() { static std::set<JSClassID> s; return s; }
void Finalizer(JSRuntime*, JSValue v) {
	JSClassID id = JS_GetClassID(v);
	const JSClass* c = nullptr;
	{ std::lock_guard<std::mutex> lock(ClassMutex()); auto it = ClassById().find(id); if (it != ClassById().end()) c = it->second; }
	if (c && c->cOps && c->cOps->finalize) c->cOps->finalize(nullptr, (JSObject*)JS_VALUE_GET_PTR(v));
	if (c && ProxyClasses().count(id)) delete (ProxyData*)JS_GetOpaque(v, id);
}
std::mutex& ClassMutex() { static std::mutex m; return m; }
JSClassDef MakeDef(const JSClass* clasp);
JSClassID EnsureClass(JSContext* cx, const JSClass* clasp) {
	// Class ids are global, but a class has to be registered in every runtime that uses it.
	std::lock_guard<std::mutex> lock(ClassMutex());
	auto& ids = ClassIds();
	JSRuntime* rt = JS_GetRuntime(cx);
	auto it = ids.find(clasp);
	JSClassID id = 0;
	if (it != ids.end()) id = it->second;
	else { JS_NewClassID(rt, &id); ids[clasp] = id; ClassById()[id] = clasp; }
	if (!JS_IsRegisteredClass(rt, id)) { JSClassDef def = MakeDef(clasp); JS_NewClass(rt, id, &def); }
	return id;
}
} // namespace

JSObject* JS_NewObject(JSContext* cx, const JSClass* clasp) {
	return PooledObj(cx, JS_NewObjectClass(cx, (int)EnsureClass(cx, clasp)));
}
JSObject* JS_NewObjectWithGivenProto(JSContext* cx, const JSClass* clasp, JS::HandleObject proto) {
	JSValue o = JS_NewObjectProtoClass(cx, proto.get() ? VO(proto) : JS_NULL, EnsureClass(cx, clasp));
	return PooledObj(cx, o);
}
bool JSShim_IsInstance(JSContext* cx, JSObject* obj, const JSClass* clasp) {
	auto it = ClassIds().find(clasp);
	return obj && it != ClassIds().end() && JS_GetClassID(VO(obj)) == it->second;
}
void* JS_GetInstancePrivate(JSContext* cx, JS::HandleObject obj, const JSClass* clasp, JS::CallArgs*) {
	auto it = ClassIds().find(clasp);
	if (!obj.get() || it == ClassIds().end()) return nullptr;
	return JS_GetOpaque(VO(obj), it->second);
}
bool JS_InstanceOf(JSContext* cx, JS::HandleObject obj, const JSClass* clasp, JS::CallArgs*) { return JSShim_IsInstance(cx, obj.get(), clasp); }
void* JS_GetPrivate(JSObject* obj) { return JS_GetOpaque(VO(obj), JS_GetClassID(VO(obj))); }
void JS_SetPrivate(JSObject* obj, void* data) { JS_SetOpaque(VO(obj), data); }
namespace {
JSClass g_cObject{"Object", 0, nullptr, nullptr, nullptr, nullptr, JSProto_Object};
JSClass g_cArray{"Array", 0, nullptr, nullptr, nullptr, nullptr, JSProto_Array};
JSClass g_cString{"String", 0, nullptr, nullptr, nullptr, nullptr, JSProto_String};
JSClass g_cNumber{"Number", 0, nullptr, nullptr, nullptr, nullptr, JSProto_Number};
JSClass g_cBoolean{"Boolean", 0, nullptr, nullptr, nullptr, nullptr, JSProto_Boolean};
JSClass g_cOther{"Other", 0, nullptr, nullptr, nullptr, nullptr, 0};
}
const JSClass* JS_GetClass(JSObject* obj) {
	JSClassID id = JS_GetClassID(VO(obj));
	{ std::lock_guard<std::mutex> lock(ClassMutex()); auto it = ClassById().find(id); if (it != ClassById().end()) return it->second; }
	static thread_local bool probed = false;
	static thread_local JSClassID idObj, idArr, idStr, idNum, idBool;
	if (!probed) {
		JSContext* cx = t_globalCx; probed = true;
		auto probe = [&](const char* code) { JSValue v = JS_Eval(cx, code, strlen(code), "<probe>", 0); JSClassID c = JS_GetClassID(v); JS_FreeValue(cx, v); return c; };
		idObj = probe("({})"); idArr = probe("[]"); idStr = probe("new String('')"); idNum = probe("new Number(0)"); idBool = probe("new Boolean(false)");
	}
	if (id == idObj) return &g_cObject; if (id == idArr) return &g_cArray; if (id == idStr) return &g_cString;
	if (id == idNum) return &g_cNumber; if (id == idBool) return &g_cBoolean; return &g_cOther;
}
JSObject* JS_InitClass(JSContext* cx, JS::HandleObject obj, JS::HandleObject parent_proto, const JSClass* clasp,
	JSNative constructor, unsigned nargs, const JSPropertySpec* ps, const JSFunctionSpec* fs,
	const JSPropertySpec* static_ps, const JSFunctionSpec* static_fs) {
	JSClassID id = EnsureClass(cx, clasp);
	JSValue proto = JS_NewObject(cx);
	JS::RootedObject protoObj(cx, PooledObj(cx, proto));
	if (ps) JS_DefineProperties(cx, protoObj, ps);
	if (fs) JS_DefineFunctions(cx, protoObj, fs);
	JS_SetClassProto(cx, id, JS_DupValue(cx, proto));
	JSValue ctor = NewNative(cx, constructor, nargs, clasp->name);
	JS_SetConstructorBit(cx, ctor, true);
	JS::RootedObject ctorObj(cx, PooledObj(cx, ctor));
	JS_SetPropertyStr(cx, ctor, "prototype", JS_DupValue(cx, proto));
	JS_SetPropertyStr(cx, proto, "constructor", JS_DupValue(cx, ctor));
	if (static_ps) JS_DefineProperties(cx, ctorObj, static_ps);
	if (static_fs) JS_DefineFunctions(cx, ctorObj, static_fs);
	Atom a(cx, clasp->name); DefProp(cx, obj.get(), a.a, ctor, 0);
	return protoObj.get();
}
JSObject* JS_GetClassObject(JSContext*, JSProtoKey, JS::MutableHandleObject objp) { objp.set(nullptr); return nullptr; }

std::wstring JSShim_StringToWString(JSContext* cx, JSString* str) {
	std::string u = JSShim_StringToUTF8(cx, str);
	std::wstring out; out.reserve(u.size());
	for (size_t i = 0; i < u.size();) {
		unsigned char c = u[i]; uint32_t cp; int n;
		if (c < 0x80) { cp = c; n = 1; } else if (c < 0xE0) { cp = c & 0x1F; n = 2; } else if (c < 0xF0) { cp = c & 0x0F; n = 3; } else { cp = c & 0x07; n = 4; }
		for (int k = 1; k < n && i + k < u.size(); k++) cp = (cp << 6) | (u[i + k] & 0x3F);
		i += n; out.push_back((wchar_t)cp);
	}
	return out;
}

// ---- Script compilation / evaluation ----
namespace {
JSValue EvalSrc(JSContext* cx, const char* code, size_t len, const char* file, bool strict) {
	std::string copy(code, len);
	return JS_Eval(cx, copy.c_str(), copy.size(), file, JS_EVAL_TYPE_GLOBAL | (strict ? JS_EVAL_FLAG_STRICT : 0));
}
thread_local int t_depth = 0;
}
bool JS::Evaluate(JSContext* cx, const CompileOptions& opts, const SourceText<mozilla::Utf8Unit>& src, MutableHandleValue rval) {
	JSValue r = EvalSrc(cx, src.data(), src.length(), opts.file, true);
	if (JS_IsException(r)) return false;
	rval.set(Pooled(cx, r)); return true;
}
JSFunction* JS::CompileFunction(JSContext* cx, RootedObjectVector&, const CompileOptions& opts, const char*, unsigned,
	const char* const*, const SourceText<mozilla::Utf8Unit>& src) {
	// Wrap on the first line so reported line numbers stay correct.
	std::string code = "(function() {" + std::string(src.data(), src.length()) + "\n})";
	JSValue r = EvalSrc(cx, code.data(), code.size(), opts.file, true);
	if (JS_IsException(r)) return nullptr;
	return PooledObj(cx, r);
}
bool JS_CallFunction(JSContext* cx, JS::HandleObject thisObj, JS::HandleObject fun, const JS::HandleValueArray& args, JS::MutableHandleValue rval) {
	std::vector<JSValue> a; for (size_t i = 0; i < args.length(); i++) a.push_back(args.begin()[i].raw());
	JSValue r = JS_Call(cx, VO(fun), thisObj.get() ? VO(thisObj) : JS_UNDEFINED, (int)a.size(), a.data());
	if (JS_IsException(r)) return false; rval.set(Pooled(cx, r)); return true;
}
JSObject* JSShim_GlobalObject(JSContext* cx) { return PooledObj(cx, JS_GetGlobalObject(cx)); }
bool JSShim_GetOwnPropFlags(JSContext* cx, JSObject* obj, const char* name, bool* found, bool* writable, bool* configurable) {
	Atom a(cx, name); JSPropertyDescriptor d;
	int r = JS_GetOwnProperty(cx, &d, VO(obj), a.a);
	if (r < 0) return false;
	*found = r > 0;
	if (r > 0) {
		*writable = (d.flags & JS_PROP_WRITABLE) || (d.flags & JS_PROP_GETSET);
		*configurable = d.flags & JS_PROP_CONFIGURABLE;
		JS_FreeValue(cx, d.value); JS_FreeValue(cx, d.getter); JS_FreeValue(cx, d.setter);
	}
	return true;
}
bool JSShim_EvalGlobalName(JSContext* cx, const char* name, JS::MutableHandleValue out) {
	// Resolves script-level let/const/class bindings, which live outside the global object.
	JSValue r = JS_Eval(cx, name, strlen(name), "<global>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r)) { JS_FreeValue(cx, JS_GetException(cx)); out.set(JS::UndefinedValue()); return true; }
	out.set(Pooled(cx, r)); return true;
}
// The temp pool is released lazily: when the *next* outermost request starts, not when one ends. Functions that
// open their own request and then return a raw JS::Value (e.g. deserializers) would otherwise hand the caller a
// value that was just freed.
thread_local bool t_poolPending = false;
void JSShim_RequestEnter() {
	if (t_depth == 0 && t_poolPending) { t_poolPending = false; if (t_globalCx) JSShim_PoolRelease(t_globalCx); }
	t_depth++;
}
// Called when a Heap/PersistentRooted drops its reference: inside a script request the value is parked in the
// temp pool (and freed when the outermost request ends) so Rooted copies made from it stay valid.
void JS::JSShim_DeferFree(JSRuntime* rt, JSValue v) {
	if (rt == t_globalRt && JS_VALUE_HAS_REF_COUNT(v)) { t_ctx.pool.push_back(v); if (t_depth == 0) t_poolPending = true; return; }
	JS_FreeValueRT(rt, v);
}
void JSShim_RequestLeave(JSContext*) { if (--t_depth == 0) t_poolPending = true; }
bool JS_Init() { return true; }
void JS_ShutDown() {}

JS::PropertyKey JS::PropertyKey::Int(int i) { return JS::PropertyKey(JS_NewAtomUInt32(t_globalCx, (uint32_t)i)); }
JS::PropertyKey JS::PropertyKey::fromPinnedString(JSString* s) { return JS::PropertyKey(JS_ValueToAtom(t_globalCx, JS::Conv<JSString*>::to(s))); }
JSString* JS_AtomizeAndPinString(JSContext* cx, const char* s) { return JS_NewStringCopyZ(cx, s); }
JS::PropertyKey INT_TO_JSID(int i) { return JS::PropertyKey(JS_NewAtomUInt32(t_globalCx, (uint32_t)i)); }

// ======== Batch implementations ========
JSType JS_TypeOfValue(JSContext* cx, JS::HandleValue v) {
	if (v.isUndefined()) return JSTYPE_UNDEFINED; if (v.isNull()) return JSTYPE_OBJECT; // as SpiderMonkey: typeof null is an object
	if (v.isBoolean()) return JSTYPE_BOOLEAN; if (v.isNumber()) return JSTYPE_NUMBER;
	if (v.isString()) return JSTYPE_STRING; if (v.isSymbol()) return JSTYPE_SYMBOL;
	if (JS_IsFunction(cx, V(v.get()))) return JSTYPE_FUNCTION;
	return JSTYPE_OBJECT;
}
int64_t JS_Now() {
	return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
namespace { JSInterruptCallback t_interruptCb = nullptr;
int InterruptThunk(JSRuntime*, void*) { return t_interruptCb && t_globalCx ? (t_interruptCb(t_globalCx) ? 0 : 1) : 0; } }
bool JS_AddInterruptCallback(JSContext* cx, JSInterruptCallback cb) {
	t_interruptCb = cb; JS_SetInterruptHandler(JS_GetRuntime(cx), cb ? InterruptThunk : nullptr, nullptr); return true;
}
namespace { thread_local std::vector<std::unique_ptr<std::u16string>> t_strKeep; void ClearStrKeep() { t_strKeep.clear(); } }
const char16_t* JS_GetTwoByteStringCharsAndLength(JSContext* cx, const JS::AutoCheckCannotGC&, JSString* str, size_t* length) {
	t_strKeep.push_back(std::make_unique<std::u16string>(JSShim_StringToUTF16(cx, str)));
	*length = t_strKeep.back()->size(); return t_strKeep.back()->data();
}
JSString* JS_NewStringCopyUTF8Z(JSContext* cx, const JS::ConstUTF8CharsZ& s) { return JS_NewStringCopyN(cx, s.p, s.n); }
JSString* JS_AtomizeAndPinUCStringN(JSContext* cx, const char16_t* chars, size_t length) { return JS_NewUCStringCopyN(cx, chars, length); }

JSObject* JS_NewUint8Array(JSContext* cx, size_t length) {
	JSValue n = JS_NewInt64(cx, (int64_t)length);
	JSValue r = JS_NewTypedArray(cx, 1, &n, JS_TYPED_ARRAY_UINT8); return PooledObj(cx, r);
}
JSObject* JS_NewUint16Array(JSContext* cx, size_t length) {
	JSValue n = JS_NewInt64(cx, (int64_t)length);
	JSValue r = JS_NewTypedArray(cx, 1, &n, JS_TYPED_ARRAY_UINT16); return PooledObj(cx, r);
}
namespace {
uint8_t* TAData(JSObject* obj, size_t* len) {
	JSContext* cx = t_globalCx; size_t off, blen, bpe;
	JSValue buf = JS_GetTypedArrayBuffer(cx, VO(obj), &off, &blen, &bpe);
	if (JS_IsException(buf)) return nullptr;
	size_t sz; uint8_t* p = JS_GetArrayBuffer(cx, &sz, buf); JS_FreeValue(cx, buf);
	if (len) *len = blen / (bpe ? bpe : 1);
	return p ? p + off : nullptr;
}
}
uint8_t* JS_GetUint8ArrayData(JSObject* obj, bool* sh, const JS::AutoCheckCannotGC&) { if (sh) *sh = false; return TAData(obj, nullptr); }
uint16_t* JS_GetUint16ArrayData(JSObject* obj, bool* sh, const JS::AutoCheckCannotGC&) { if (sh) *sh = false; return (uint16_t*)TAData(obj, nullptr); }
size_t JS_GetTypedArrayLength(JSObject* obj) { size_t n = 0; TAData(obj, &n); return n; }
size_t JS_GetTypedArrayByteOffset(JSObject* obj) {
	size_t off, blen, bpe; JSValue buf = JS_GetTypedArrayBuffer(t_globalCx, VO(obj), &off, &blen, &bpe);
	JS_FreeValue(t_globalCx, buf); return off;
}
js::Scalar::Type JS_GetArrayBufferViewType(JSObject* obj) {
	switch (JS_GetTypedArrayType(VO(obj))) {
	case JS_TYPED_ARRAY_UINT8C: return js::Scalar::Uint8Clamped; case JS_TYPED_ARRAY_INT8: return js::Scalar::Int8;
	case JS_TYPED_ARRAY_UINT8: return js::Scalar::Uint8; case JS_TYPED_ARRAY_INT16: return js::Scalar::Int16;
	case JS_TYPED_ARRAY_UINT16: return js::Scalar::Uint16; case JS_TYPED_ARRAY_INT32: return js::Scalar::Int32;
	case JS_TYPED_ARRAY_UINT32: return js::Scalar::Uint32; case JS_TYPED_ARRAY_FLOAT32: return js::Scalar::Float32;
	case JS_TYPED_ARRAY_FLOAT64: return js::Scalar::Float64; default: return js::Scalar::MaxTypedArrayViewType; }
}
JSObject* JS_GetArrayBufferViewBuffer(JSContext* cx, JS::HandleObject obj, bool* sh) {
	if (sh) *sh = false; size_t off, blen, bpe;
	return PooledObj(cx, JS_GetTypedArrayBuffer(cx, VO(obj), &off, &blen, &bpe));
}
bool JS::IsArrayBufferObject(JSObject* obj) { return obj && JS_IsArrayBuffer(VO(obj)); }
uint8_t* JS::GetArrayBufferData(JSObject* obj, bool* sh, const AutoCheckCannotGC&) { if (sh) *sh = false; size_t n; return JS_GetArrayBuffer(t_globalCx, &n, VO(obj)); }
size_t JS::GetArrayBufferByteLength(JSObject* obj) { size_t n = 0; JS_GetArrayBuffer(t_globalCx, &n, VO(obj)); return n; }
JSObject* JS::NewArrayBufferWithContents(JSContext* cx, size_t nbytes, void* contents) {
	JSValue r = JS_NewArrayBufferCopy(cx, (const uint8_t*)contents, nbytes); free(contents); return PooledObj(cx, r);
}
JSObject* JSShim_NewTypedArrayWithBuffer(JSContext* cx, int type, JS::HandleObject buffer, size_t byteOffset, int64_t length) {
	JSValue a[3] = { VO(buffer), JS_NewInt64(cx, (int64_t)byteOffset), JS_NewInt64(cx, length) };
	return PooledObj(cx, JS_NewTypedArray(cx, 3, a, (JSTypedArrayEnum)type));
}

// ======== Proxy objects ========
namespace {
ProxyData* PD(JSValueConst obj) { return (ProxyData*)JS_GetOpaque(obj, JS_GetClassID(obj)); }
JSObject* PObj(JSValueConst o) { return (JSObject*)JS_VALUE_GET_PTR(o); }

JSValue px_get(JSContext* cx, JSValueConst obj, JSAtom atom, JSValueConst receiver) {
	ProxyData* d = PD(obj); if (!d) return JS_UNDEFINED;
	JS::RootedObject proxy(cx, PObj(obj)); JS::RootedValue recv(cx, JS::Value(receiver)), vp(cx); JS::RootedId id(cx, JS::PropertyKey(atom));
	JSShim_RequestEnter();
	bool ok = d->handler->get(cx, proxy, recv, id, &vp);
	JSValue r = ok ? JS_DupValue(cx, vp.get().raw()) : JS_EXCEPTION;
	JSShim_RequestLeave(cx);
	if (!ok && !JS_HasException(cx)) JS_ThrowInternalError(cx, "GUI proxy get failed");
	return r;
}
int px_set(JSContext* cx, JSValueConst obj, JSAtom atom, JSValueConst value, JSValueConst receiver, int flags) {
	ProxyData* d = PD(obj); if (!d) return 0;
	JS::RootedObject proxy(cx, PObj(obj)); JS::RootedValue v(cx, JS::Value(value)), recv(cx, JS::Value(receiver)); JS::RootedId id(cx, JS::PropertyKey(atom));
	JS::ObjectOpResult res;
	JSShim_RequestEnter();
	bool ok = d->handler->set(cx, proxy, id, v, recv, res);
	JSShim_RequestLeave(cx);
	if (!ok) { if (!JS_HasException(cx)) JS_ThrowInternalError(cx, "GUI proxy set failed"); return -1; }
	if (!res.ok() && !JS_HasException(cx) && (flags & (JS_PROP_THROW | JS_PROP_THROW_STRICT)))
		JS_ThrowTypeError(cx, "cannot set property on GUI object");
	return res.ok() ? 1 : (JS_HasException(cx) ? -1 : 0);
}
int px_delete(JSContext* cx, JSValueConst obj, JSAtom atom) {
	ProxyData* d = PD(obj); if (!d) return 0;
	JS::RootedObject proxy(cx, PObj(obj)); JS::RootedId id(cx, JS::PropertyKey(atom)); JS::ObjectOpResult res;
	JSShim_RequestEnter();
	bool ok = d->handler->delete_(cx, proxy, id, res);
	JSShim_RequestLeave(cx);
	if (!ok) return -1;
	return res.ok() ? 1 : 0;
}
int px_get_own_property(JSContext*, JSPropertyDescriptor*, JSValueConst, JSAtom) { return 0; }
int px_has_property(JSContext*, JSValueConst, JSAtom) { return 0; }
JSClassExoticMethods g_exotic = { px_get_own_property, nullptr, px_delete, nullptr, px_has_property, px_get, px_set };
JSClassDef MakeDef(const JSClass* clasp) {
	JSClassDef def = {}; def.class_name = clasp->name; def.finalizer = Finalizer;
	return def;
}
} // namespace

JSObject* js::NewProxyObject(JSContext* cx, const BaseProxyHandler* handler, JS::HandleValue priv, JSObject*, const ProxyOptions& options) {
	// Proxy classes are registered like ordinary classes, then given exotic behaviour via a dedicated def.
	JSClassID id;
	{
		std::lock_guard<std::mutex> lock(ClassMutex());
		auto& ids = ClassIds(); JSRuntime* rt = JS_GetRuntime(cx);
		auto it = ids.find(options.clasp);
		if (it != ids.end()) id = it->second; else { id = 0; JS_NewClassID(rt, &id); ids[options.clasp] = id; ClassById()[id] = options.clasp; }
		ProxyClasses().insert(id);
		if (!JS_IsRegisteredClass(rt, id)) { JSClassDef def = MakeDef(options.clasp); def.exotic = &g_exotic; JS_NewClass(rt, id, &def); }
	}
	JSValue o = JS_NewObjectClass(cx, (int)id);
	if (JS_IsException(o)) return nullptr;
	ProxyData* d = new ProxyData{ handler, priv.get().raw(), { JS_UNDEFINED, JS_UNDEFINED } };
	JS_SetOpaque(o, d);
	return PooledObj(cx, o);
}
JS::Value js::GetProxyPrivate(const JSObject* obj) { return JS::Value(PD(JS::Conv<JSObject*>::to(const_cast<JSObject*>(obj)))->priv); }
JS::Value js::GetProxyReservedSlot(const JSObject* obj, size_t slot) { return JS::Value(PD(JS::Conv<JSObject*>::to(const_cast<JSObject*>(obj)))->slots[slot]); }
void js::SetProxyReservedSlot(JSObject* obj, size_t slot, const JS::Value& v) { PD(JS::Conv<JSObject*>::to(obj))->slots[slot] = v.raw(); }

// ======== Collections, iteration, promises ========
namespace {
bool CallMethod(JSContext* cx, JSValueConst obj, const char* name, int argc, JSValueConst* argv, JSValue* out) {
	JSValue r = JS_Invoke(cx, obj, JS_NewAtom(cx, name), argc, argv);
	if (JS_IsException(r)) return false;
	if (out) *out = r; else JS_FreeValue(cx, r);
	return true;
}
bool InstanceOfGlobal(JSContext* cx, JSObject* obj, const char* ctorName) {
	JSValue g = JS_GetGlobalObject(cx); JSValue c = JS_GetPropertyStr(cx, g, ctorName);
	int r = JS_IsInstanceOf(cx, VO(obj), c); JS_FreeValue(cx, c); JS_FreeValue(cx, g); return r > 0;
}
uint32_t SizeOf(JSContext* cx, JSObject* obj) {
	JSValue sz = JS_GetPropertyStr(cx, VO(obj), "size"); uint32_t n = 0; JS_ToUint32(cx, &n, sz); JS_FreeValue(cx, sz); return n;
}
JSObject* NewGlobalCtor(JSContext* cx, const char* name) {
	JSValue g = JS_GetGlobalObject(cx); JSValue c = JS_GetPropertyStr(cx, g, name);
	JSValue o = JS_CallConstructor(cx, c, 0, nullptr); JS_FreeValue(cx, c); JS_FreeValue(cx, g); return PooledObj(cx, o);
}
std::map<JSObject*, std::pair<JSValue, JSValue>>& PromiseFuncs() { static thread_local std::map<JSObject*, std::pair<JSValue, JSValue>> m; return m; }
}
bool JS::IsArray(JSContext*, HandleObject obj, bool* r) { *r = obj.get() && JS_IsArray(VO(obj)); return true; }
bool JS::IsMapObject(JSContext* cx, HandleObject obj, bool* r) { *r = obj.get() && InstanceOfGlobal(cx, obj.get(), "Map"); return true; }
bool JS::IsSetObject(JSContext* cx, HandleObject obj, bool* r) { *r = obj.get() && InstanceOfGlobal(cx, obj.get(), "Set"); return true; }
uint32_t JS::MapSize(JSContext* cx, HandleObject obj) { return SizeOf(cx, obj.get()); }
uint32_t JS::SetSize(JSContext* cx, HandleObject obj) { return SizeOf(cx, obj.get()); }
bool JS::MapEntries(JSContext* cx, HandleObject obj, MutableHandleValue rval) {
	JSValue r; if (!CallMethod(cx, VO(obj), "entries", 0, nullptr, &r)) return false; rval.set(Pooled(cx, r)); return true;
}
bool JS::SetValues(JSContext* cx, HandleObject obj, MutableHandleValue rval) {
	JSValue r; if (!CallMethod(cx, VO(obj), "values", 0, nullptr, &r)) return false; rval.set(Pooled(cx, r)); return true;
}
JSObject* JS::NewMapObject(JSContext* cx) { return NewGlobalCtor(cx, "Map"); }
JSObject* JS::NewSetObject(JSContext* cx) { return NewGlobalCtor(cx, "Set"); }
bool JS::MapSet(JSContext* cx, HandleObject obj, HandleValue key, HandleValue val) {
	JSValue a[2] = { V(key.get()), V(val.get()) }; return CallMethod(cx, VO(obj), "set", 2, a, nullptr);
}
bool JS::SetAdd(JSContext* cx, HandleObject obj, HandleValue key) { JSValue a[1] = { V(key.get()) }; return CallMethod(cx, VO(obj), "add", 1, a, nullptr); }

bool JS::ForOfIterator::init(HandleValue iterable, NonIterableBehavior) {
	JSValue symIter; { JSValue g = JS_GetGlobalObject(cx); JSValue S = JS_GetPropertyStr(cx, g, "Symbol"); symIter = JS_GetPropertyStr(cx, S, "iterator"); JS_FreeValue(cx, S); JS_FreeValue(cx, g); }
	JSAtom a = JS_ValueToAtom(cx, symIter); JS_FreeValue(cx, symIter);
	JSValue fn = JS_GetProperty(cx, V(iterable.get()), a); JS_FreeAtom(cx, a);
	if (JS_IsException(fn)) return false;
	JSValue it = JS_Call(cx, fn, V(iterable.get()), 0, nullptr); JS_FreeValue(cx, fn);
	if (JS_IsException(it)) return false;
	iterator = Pooled(cx, it); return true;
}
bool JS::ForOfIterator::next(MutableHandleValue val, bool* done) {
	JSValue r; if (!CallMethod(cx, V(iterator), "next", 0, nullptr, &r)) return false;
	Value rv = Pooled(cx, r);
	JSValue d = JS_GetPropertyStr(cx, r, "done"); *done = JS_ToBool(cx, d) > 0; JS_FreeValue(cx, d);
	JSValue v = JS_GetPropertyStr(cx, r, "value"); val.set(Pooled(cx, v)); return true;
}
JSObject* JS::NewPromiseObject(JSContext* cx, HandleObject) {
	JSValue funcs[2]; JSValue p = JS_NewPromiseCapability(cx, funcs);
	if (JS_IsException(p)) return nullptr;
	JSObject* po = PooledObj(cx, p);
	PromiseFuncs()[po] = { funcs[0], funcs[1] };
	return po;
}
bool JS::ResolvePromise(JSContext* cx, HandleObject promise, HandleValue v) {
	auto it = PromiseFuncs().find(promise.get()); if (it == PromiseFuncs().end()) return false;
	JSValue a = V(v.get()); JSValue r = JS_Call(cx, it->second.first, JS_UNDEFINED, 1, &a);
	JS_FreeValue(cx, r); JS_FreeValue(cx, it->second.first); JS_FreeValue(cx, it->second.second); PromiseFuncs().erase(it); return true;
}
bool JS::RejectPromise(JSContext* cx, HandleObject promise, HandleValue v) {
	auto it = PromiseFuncs().find(promise.get()); if (it == PromiseFuncs().end()) return false;
	JSValue a = V(v.get()); JSValue r = JS_Call(cx, it->second.second, JS_UNDEFINED, 1, &a);
	JS_FreeValue(cx, r); JS_FreeValue(cx, it->second.first); JS_FreeValue(cx, it->second.second); PromiseFuncs().erase(it); return true;
}

bool JS_Enumerate(JSContext* cx, JS::HandleObject obj, JS::MutableHandle<JS::IdVector> props) {
	JSPropertyEnum* tab; uint32_t n;
	if (JS_GetOwnPropertyNames(cx, &tab, &n, VO(obj), JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0) return false;
	for (uint32_t i = 0; i < n; i++) props.get().push_back(JS::PropertyKey(JS_DupAtom(cx, tab[i].atom)));
	JS_FreePropertyEnum(cx, tab, n); return true;
}
bool JS_GetPropertyDescriptorById(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
	JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> desc, JS::MutableHandleObject holder) {
	JSValue cur = JS_DupValue(cx, VO(obj));
	while (JS_IsObject(cur)) {
		JSPropertyDescriptor d; int r = JS_GetOwnProperty(cx, &d, cur, id->atom);
		if (r < 0) { JS_FreeValue(cx, cur); return false; }
		if (r > 0) {
			JS::PropertyDescriptor pd; pd.value = JS::Value(d.value); 
			pd.attrs = ((d.flags & JS_PROP_WRITABLE) ? 0 : JSPROP_READONLY) | ((d.flags & JS_PROP_ENUMERABLE) ? JSPROP_ENUMERATE : 0) | ((d.flags & JS_PROP_CONFIGURABLE) ? 0 : JSPROP_PERMANENT);
			pd.hasGetter_ = !JS_IsUndefined(d.getter); pd.hasSetter_ = !JS_IsUndefined(d.setter);
			JS_FreeValue(cx, d.getter); JS_FreeValue(cx, d.setter); JS_FreeValue(cx, d.value);
			desc.get().emplace(pd); holder.set(PooledObj(cx, cur)); return true;
		}
		JSValue p = JS_GetPrototype(cx, cur); JS_FreeValue(cx, cur); cur = p;
	}
	JS_FreeValue(cx, cur); desc.get().reset(); return true;
}

JSFunction* JS_ValueToFunction(JSContext* cx, JS::HandleValue v) { return (v.isObject() && JS_IsFunction(cx, V(v.get()))) ? v.toObjectOrNull() : nullptr; }
JSString* JS_GetFunctionId(JSFunction* fun) {
	JSContext* cx = t_globalCx; JSValue n = JS_GetPropertyStr(cx, VO(fun), "name");
	if (!JS_IsString(n)) { JS_FreeValue(cx, n); return nullptr; }
	return JS::Conv<JSString*>::from(Pooled(cx, n).raw());
}

// ======== Prelude: SpiderMonkey non-standard globals ========
void JSShim_InstallPrelude(JSContext* cx) {
	static const char* kPrelude = R"JS(
(function (g) {
	function q(s) { return JSON.stringify(s); }
	function key(k) { return /^[A-Za-z_$][\w$]*$/.test(k) ? k : q(k); }
	function u(v, seen) {
		switch (typeof v) {
		case 'undefined': return 'undefined';
		case 'boolean': return String(v);
		case 'number': return (v === 0 && 1 / v < 0) ? '-0' : String(v);
		case 'bigint': return String(v) + 'n';
		case 'string': return q(v);
		case 'symbol': return String(v);
		case 'function': return Function.prototype.toString.call(v);
		}
		if (v === null) return 'null';
		if (seen.indexOf(v) >= 0) return '({})';
		seen.push(v);
		let r;
		if (Array.isArray(v)) r = '[' + Array.prototype.map.call(v, e => u(e, seen)).join(', ') + ']';
		else if (v instanceof Date) r = '(new Date(' + v.getTime() + '))';
		else if (v instanceof RegExp) r = String(v);
		else if (v instanceof Map) r = '(new Map([' + Array.from(v, e => '[' + u(e[0], seen) + ', ' + u(e[1], seen) + ']').join(', ') + ']))';
		else if (v instanceof Set) r = '(new Set([' + Array.from(v, e => u(e, seen)).join(', ') + ']))';
		else r = '({' + Object.keys(v).map(k => key(k) + ':' + u(v[k], seen)).join(', ') + '})';
		seen.pop();
		return r;
	}
	g.uneval = function uneval(v) { return u(v, []); };
	// Structured clone helpers: QuickJS' object writer cannot handle Map/Set, so those are rewritten to tagged
	// plain objects (keeping shared/cyclic references) before writing and rebuilt after reading.
	g.__oadTag = function (root) {
		const memo = new Map();
		function tag(v) {
			if (v === null || typeof v !== 'object') return v;
			if (memo.has(v)) return memo.get(v);
			let o;
			if (v instanceof Map) { o = { __oad: 'map', e: [] }; memo.set(v, o); for (const [k, x] of v) o.e.push([tag(k), tag(x)]); return o; }
			if (v instanceof Set) { o = { __oad: 'set', e: [] }; memo.set(v, o); for (const x of v) o.e.push(tag(x)); return o; }
			if (v instanceof Date || v instanceof ArrayBuffer || ArrayBuffer.isView(v)) { memo.set(v, v); return v; }
			if (Array.isArray(v)) { o = new Array(v.length); memo.set(v, o); for (let i = 0; i < v.length; i++) o[i] = tag(v[i]); return o; }
			o = {}; memo.set(v, o); for (const k of Object.keys(v)) o[k] = tag(v[k]); return o;
		}
		return tag(root);
	};
	g.__oadUntag = function (root) {
		const memo = new Map();
		function untag(v) {
			if (v === null || typeof v !== 'object') return v;
			if (memo.has(v)) return memo.get(v);
			if (v.__oad === 'map') { const m = new Map(); memo.set(v, m); for (const [k, x] of v.e) m.set(untag(k), untag(x)); return m; }
			if (v.__oad === 'set') { const m = new Set(); memo.set(v, m); for (const x of v.e) m.add(untag(x)); return m; }
			memo.set(v, v);
			if (v instanceof Date || v instanceof ArrayBuffer || ArrayBuffer.isView(v)) return v;
			if (Array.isArray(v)) { for (let i = 0; i < v.length; i++) v[i] = untag(v[i]); return v; }
			for (const k of Object.keys(v)) v[k] = untag(v[k]);
			return v;
		}
		return untag(root);
	};
	Object.defineProperty(Object.prototype, 'toSource', { value: function toSource() { return u(this, []); }, writable: true, configurable: true, enumerable: false });
})(globalThis);
)JS";
	JSValue r = JS_Eval(cx, kPrelude, strlen(kPrelude), "<prelude>", JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(r)) { JSValue e = JS_GetException(cx); const char* m = JS_ToCString(cx, e); fprintf(stderr, "prelude failed: %s\n", m ? m : "?"); JS_FreeValue(cx, e); }
	JS_FreeValue(cx, r);
}
