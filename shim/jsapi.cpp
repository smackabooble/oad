#include "js/jsapi.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>

namespace JS {
const Value UndefinedHandleValueStorage;
const Value NullHandleValueStorage(JS_NULL);
const Value TrueHandleValueStorage(JS_TRUE);
const Value FalseHandleValueStorage(JS_FALSE);
}

namespace {
thread_local JSShimCtx t_ctx;
thread_local JSContext* t_globalCx = nullptr;
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
	JSValue data = JS_MKVAL(JS_TAG_INT, (int32_t)(intptr_t)call);
	JSValue f = JS_NewCFunctionData(cx, Trampoline, (int)nargs, 0, 1, &data);
	if (name) { JSValue n = JS_NewString(cx, name); JS_DefinePropertyValueStr(cx, f, "name", n, JS_PROP_CONFIGURABLE); }
	return f;
}
} // namespace

JSShimCtx& JSShim_Ctx(JSContext*) { return t_ctx; }
void JSShim_PoolRelease(JSContext* cx, size_t keep) {
	auto& p = t_ctx.pool;
	while (p.size() > keep) { JS_FreeValue(cx, p.back()); p.pop_back(); }
}
JSContext* JS::JSShim_GlobalCx() { return t_globalCx; }
void JSShim_SetGlobalCx(JSContext* cx) { t_globalCx = cx; }

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
bool ToBoolean(HandleValue v) { return JS_ToBool(nullptr, V(v)) > 0; }
bool CallArgs::requireAtLeast(JSContext* cx, const char* fn, unsigned required) const {
	if (argc_ >= required) return true;
	JS_ThrowTypeError(cx, "%s requires at least %u argument(s)", fn, required); return false;
}
} // namespace JS
bool JS_ObjectIsFunction(JSObject* obj) { return obj && JS_IsFunction(nullptr, VO(obj)); }

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
std::map<const JSClass*, JSClassID>& ClassIds() { static std::map<const JSClass*, JSClassID> m; return m; }
std::map<JSClassID, const JSClass*>& ClassById() { static std::map<JSClassID, const JSClass*> m; return m; }
void Finalizer(JSRuntime*, JSValue v) {
	JSClassID id = JS_GetClassID(v);
	auto it = ClassById().find(id);
	if (it != ClassById().end() && it->second->cOps && it->second->cOps->finalize)
		it->second->cOps->finalize(nullptr, (JSObject*)JS_VALUE_GET_PTR(v));
}
JSClassID EnsureClass(JSContext* cx, const JSClass* clasp) {
	auto& ids = ClassIds();
	auto it = ids.find(clasp);
	if (it != ids.end()) {
		if (!JS_IsRegisteredClass(JS_GetRuntime(cx), it->second)) {} // same runtime only
		return it->second;
	}
	JSClassID id = 0; JS_NewClassID(JS_GetRuntime(cx), &id);
	JSClassDef def = {}; def.class_name = clasp->name; def.finalizer = Finalizer;
	JS_NewClass(JS_GetRuntime(cx), id, &def);
	ids[clasp] = id; ClassById()[id] = clasp;
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
const JSClass* JS_GetClass(JSObject* obj) {
	auto it = ClassById().find(JS_GetClassID(VO(obj))); return it == ClassById().end() ? nullptr : it->second;
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
