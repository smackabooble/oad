// Part 3 of the shim: natives, classes, object/property/function API. Included from jsapi.h.
#pragma once
#include <string_view>

using uint = unsigned int;
using JSNative = bool (*)(JSContext*, unsigned, JS::Value*);

// Property attribute flags (values as in SpiderMonkey 91).
enum : unsigned {
	JSPROP_ENUMERATE = 0x01, JSPROP_READONLY = 0x02, JSPROP_PERMANENT = 0x04,
	JSPROP_RESOLVING = 0x08, JSPROP_GETTER = 0x10, JSPROP_SETTER = 0x20,
};
#define JSCLASS_HAS_PRIVATE 0x1u
#define JSCLASS_GLOBAL_FLAGS 0x2u
#define JSCLASS_FOREGROUND_FINALIZE 0x4u
#define JSCLASS_HAS_RESERVED_SLOTS(n) 0x8u
#define JSCLASS_HAS_CACHED_PROTO(k) 0x10u

enum JSProtoKey { JSProto_Null = 0, JSProto_Object, JSProto_Array, JSProto_Proxy, JSProto_Error, JSProto_String, JSProto_Number, JSProto_Boolean, JSProto_Date, JSProto_Map, JSProto_Set, JSProto_LIMIT };
#define JSCLASS_CACHED_PROTO_KEY(clasp) ((JSProtoKey)((clasp)->protoKey))
#define JSVAL_INT_MAX 2147483647
#define JSVAL_INT_MIN (-2147483647 - 1)
struct JSTracer;
struct JSFreeOp;
using JSFinalizeOp = void (*)(JSFreeOp*, JSObject*);
using JSTraceOp = void (*)(JSTracer*, JSObject*);
struct JSClassOps {
	void* addProperty = nullptr;
	void* delProperty = nullptr;
	void* enumerate = nullptr;
	void* newEnumerate = nullptr;
	void* resolve = nullptr;
	void* mayResolve = nullptr;
	JSFinalizeOp finalize = nullptr;
	JSNative call = nullptr;
	void* hasInstance = nullptr;
	JSNative construct = nullptr;
	JSTraceOp trace = nullptr;
};
struct JSClass {
	const char* name;
	uint32_t flags;
	const JSClassOps* cOps;
	void* spec = nullptr;
	void* ext = nullptr;
	void* oOps = nullptr;
	int protoKey = 0;
};

struct JSFunctionSpec { const char* name; JSNative call; uint16_t nargs; uint16_t flags; };
struct JSPropertySpec {
	const char* name; JSNative getter; JSNative setter; uint8_t flags;
};
#define JS_FN(name, call, nargs, flags) { name, call, (uint16_t)(nargs), (uint16_t)(flags) }
#define JS_FS_END { nullptr, nullptr, 0, 0 }
#define JS_PS_END { nullptr, nullptr, nullptr, 0 }
#define JS_PSGS(name, getter, setter, flags) { name, getter, setter, (uint8_t)(flags) }
#define JS_PSG(name, getter, flags) { name, getter, nullptr, (uint8_t)(flags) }

namespace JS {

class CallArgs {
	Value* argv_;   // argv_[-2] = callee/rval, argv_[-1] = this
	unsigned argc_;
public:
	CallArgs(unsigned argc, Value* vp) : argv_(vp + 2), argc_(argc) {}
	unsigned length() const { return argc_; }
	Value* array() const { return argv_; }
	bool hasDefined(unsigned i) const { return i < argc_ && !argv_[i].isUndefined(); }
	HandleValue get(unsigned i) const { return i < argc_ ? HandleValue(argv_[i]) : HandleValue(UndefinedHandleValueStorage); }
	HandleValue operator[](unsigned i) const { return get(i); }
	HandleValue thisv() const { return HandleValue(argv_[-1]); }
	HandleValue calleev() const { return HandleValue(argv_[-2]); }
	MutableHandleValue rval() const { return MutableHandleValue(&argv_[-2]); }
	bool isConstructing() const { return false; }
	bool requireAtLeast(JSContext* cx, const char* fnname, unsigned required) const;
};
inline CallArgs CallArgsFromVp(unsigned argc, Value* vp) { return CallArgs(argc, vp); }

class HandleValueArray {
	const Value* p; size_t n;
public:
	HandleValueArray(const Value& single) : p(&single), n(1) {}
	HandleValueArray(const Rooted<Value>& single) : p(&single.get()), n(1) {}
	HandleValueArray(HandleValue single) : p(&single.get()), n(1) {}
	HandleValueArray(size_t len, const Value* elems) : p(elems), n(len) {}
	static HandleValueArray empty() { return HandleValueArray(0, nullptr); }
	size_t length() const { return n; }
	const Value* begin() const { return p; }
	HandleValue operator[](size_t i) const { return HandleValue(p[i]); }
};

// Vector of rooted values (RootedValueVector); liveness via the temp pool.
class MutableHandleValueVector;
class RootedVector_ : public std::vector<Value> {
public:
	MutableHandleValueVector operator&();
	explicit RootedVector_(JSContext*) {}
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<const U&, Value>>>
	bool append(const U& v) { push_back(static_cast<const Value&>(v)); return true; }
	bool appendAll(const std::vector<Value>& o) { insert(end(), o.begin(), o.end()); return true; }
	size_t length() const { return size(); }
	bool resize(size_t n) { std::vector<Value>::resize(n); return true; }
	bool reserve(size_t n) { std::vector<Value>::reserve(n); return true; }
	operator HandleValueArray() const { return HandleValueArray(size(), data()); }
};
using RootedValueVector = RootedVector_;
class RootedObjectVector : public std::vector<JSObject*> { public: explicit RootedObjectVector(JSContext*) {} };

class ObjectOpResult {
	bool ok_ = true;
public:
	bool ok() const { return ok_; }
	bool succeed() { ok_ = true; return true; }
	bool fail(int) { ok_ = false; return true; }
	bool failCantSetInterposed() { return fail(0); }
	explicit operator bool() const { return ok_; }
};

struct Realm;
class MutableHandleValueVector {
	RootedVector_* p;
public:
	MutableHandleValueVector(RootedVector_* v) : p(v) {}
	MutableHandleValue operator[](size_t i) const { return MutableHandleValue(&(*p)[i]); }
	size_t length() const { return p->size(); }
};
} // namespace JS

// ---- Context / lifecycle ----
bool JS_Init();
void JS_ShutDown();
JSContext* JSShim_NewContext(JSRuntime* rt);

// ---- Objects / properties ----
JSObject* JS_NewPlainObject(JSContext* cx);
JSObject* JS_NewObject(JSContext* cx, const JSClass* clasp);
JSObject* JS_NewObjectWithGivenProto(JSContext* cx, const JSClass* clasp, JS::HandleObject proto);
JSObject* JS_DefineObject(JSContext* cx, JS::HandleObject obj, const char* name, const JSClass* clasp, unsigned attrs);

bool JS_SetProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleValue v);
bool JS_GetProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::MutableHandleValue vp);
bool JS_HasProperty(JSContext* cx, JS::HandleObject obj, const char* name, bool* found);
bool JS_SetUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* name, size_t namelen, JS::HandleValue v);
bool JS_GetUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* name, size_t namelen, JS::MutableHandleValue vp);
bool JS_HasUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* name, size_t namelen, bool* found);
bool JS_SetElement(JSContext* cx, JS::HandleObject obj, uint32_t index, JS::HandleValue v);
bool JS_GetElement(JSContext* cx, JS::HandleObject obj, uint32_t index, JS::MutableHandleValue vp);
bool JS_HasElement(JSContext* cx, JS::HandleObject obj, uint32_t index, bool* found);
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleValue v, unsigned attrs);
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleObject v, unsigned attrs);
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, int32_t v, unsigned attrs);
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, uint32_t v, unsigned attrs);
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, double v, unsigned attrs);
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, bool v, unsigned attrs);
bool JS_DefineProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::HandleString v, unsigned attrs);
bool JS_DefinePropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, JS::HandleValue v, unsigned attrs);
bool JS_DefineUCProperty(JSContext* cx, JS::HandleObject obj, const char16_t* name, size_t namelen, JS::HandleValue v, unsigned attrs);
bool JS_DefineProperties(JSContext* cx, JS::HandleObject obj, const JSPropertySpec* ps);
bool JS_DeleteProperty(JSContext* cx, JS::HandleObject obj, const char* name);
bool JS_DeleteProperty(JSContext* cx, JS::HandleObject obj, const char* name, JS::ObjectOpResult& result);
bool JS_GetPropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, JS::MutableHandleValue vp);
bool JS_SetPropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, JS::HandleValue v);
bool JS_HasPropertyById(JSContext* cx, JS::HandleObject obj, JS::HandleId id, bool* found);
bool JS_IdToValue(JSContext* cx, JS::PropertyKey id, JS::MutableHandleValue vp);
bool JS_ValueToId(JSContext* cx, JS::HandleValue v, JS::MutableHandleId idp);
bool JS_GetPrototype(JSContext* cx, JS::HandleObject obj, JS::MutableHandleObject result);
bool JS_SetPrototype(JSContext* cx, JS::HandleObject obj, JS::HandleObject proto);
bool JS_FreezeObject(JSContext* cx, JS::HandleObject obj);
bool JS_DeepFreezeObject(JSContext* cx, JS::HandleObject obj);

bool JS_IsTypedArrayObject(JSObject* obj);
bool JS_ValueToObject(JSContext* cx, JS::HandleValue v, JS::MutableHandleObject objp);

// ---- Functions ----
using JSFunction = JSObject;
namespace JS { using RootedFunction = Rooted<JSFunction*>; using HandleFunction = Handle<JSFunction*>; }
JSFunction* JS_DefineFunction(JSContext* cx, JS::HandleObject obj, const char* name, JSNative call, unsigned nargs, unsigned attrs);
bool JS_DefineFunctions(JSContext* cx, JS::HandleObject obj, const JSFunctionSpec* fs);
JSFunction* JS_NewFunction(JSContext* cx, JSNative call, unsigned nargs, unsigned flags, const char* name);
bool JS_CallFunctionName(JSContext* cx, JS::HandleObject obj, const char* name, const JS::HandleValueArray& args, JS::MutableHandleValue rval);
bool JS_CallFunctionValue(JSContext* cx, JS::HandleObject obj, JS::HandleValue fval, const JS::HandleValueArray& args, JS::MutableHandleValue rval);
namespace JS {
bool Call(JSContext* cx, HandleValue thisv, HandleValue fun, const HandleValueArray& args, MutableHandleValue rval);
bool Call(JSContext* cx, HandleObject thisv, HandleValue fun, const HandleValueArray& args, MutableHandleValue rval);
bool Construct(JSContext* cx, HandleValue fun, const HandleValueArray& args, MutableHandleObject obj);
}
bool JS_ObjectIsFunction(JSObject* obj);

// ---- Arrays ----
namespace JS {
JSObject* NewArrayObject(JSContext* cx, size_t length);
JSObject* NewArrayObject(JSContext* cx, const HandleValueArray& contents);
bool GetArrayLength(JSContext* cx, HandleObject obj, uint32_t* lengthp);
bool IsArrayObject(JSContext* cx, HandleObject obj, bool* isArray);
bool IsArrayObject(JSContext* cx, HandleValue v, bool* isArray);
}

// ---- Strings ----
JSString* JS_NewStringCopyN(JSContext* cx, const char* s, size_t n);
JSString* JS_NewStringCopyZ(JSContext* cx, const char* s);
JSString* JS_NewUCStringCopyN(JSContext* cx, const char16_t* s, size_t n);
JSString* JS_NewUCStringCopyZ(JSContext* cx, const char16_t* s);
JSString* JS_ValueToSource(JSContext* cx, JS::HandleValue v);
namespace JS {
JSString* ToString(JSContext* cx, HandleValue v);
bool ToNumber(JSContext* cx, HandleValue v, double* out);
bool ToInt32(JSContext* cx, HandleValue v, int32_t* out);
bool ToUint32(JSContext* cx, HandleValue v, uint32_t* out);
bool ToUint16(JSContext* cx, HandleValue v, uint16_t* out);
bool ToInt64(JSContext* cx, HandleValue v, int64_t* out);
bool ToBoolean(HandleValue v);
}
bool JS_StringHasLatin1Chars(JSString* str);
size_t JS_GetStringLength(JSString* str);
std::string JSShim_StringToUTF8(JSContext* cx, JSString* str);
std::u16string JSShim_StringToUTF16(JSContext* cx, JSString* str);
std::wstring JSShim_StringToWString(JSContext* cx, JSString* str);

// ---- Exceptions ----
void JS_ReportErrorUTF8(JSContext* cx, const char* fmt, ...);
void JS_ReportErrorASCII(JSContext* cx, const char* fmt, ...);
bool JS_IsExceptionPending(JSContext* cx);
bool JS_GetPendingException(JSContext* cx, JS::MutableHandleValue vp);
void JS_SetPendingException(JSContext* cx, JS::HandleValue v);
void JS_ClearPendingException(JSContext* cx);

// ---- GC (no-ops: QuickJS is refcounted, cycles collected on demand) ----
void JS_MaybeGC(JSContext* cx);
void JS_GC(JSContext* cx);

inline JS::MutableHandleValueVector JS::RootedVector_::operator&() { return MutableHandleValueVector(this); }

// ---- JSON ----
using JSONWriteCallback = bool (*)(const char16_t* buf, uint32_t len, void* data);
bool JS_ParseJSON(JSContext* cx, const char16_t* chars, uint32_t len, JS::MutableHandleValue vp);
bool JS_Stringify(JSContext* cx, JS::MutableHandleValue value, JS::HandleObject replacer, JS::HandleValue space,
	JSONWriteCallback callback, void* data);

// ---- Property enumeration ----
#define JSITER_HIDDEN 0x8u
#define JSITER_OWNONLY 0x20u
namespace JS {
class RootedIdVector : public std::vector<PropertyKey> {
public:
	explicit RootedIdVector(JSContext*) {}
	RootedIdVector(const RootedIdVector&) = default;
	size_t length() const { return size(); }
	bool append(const PropertyKey& k) { push_back(k); return true; }
};
using IdVector = RootedIdVector;
template<typename D, bool M> size_t Ops<RootedIdVector, D, M>::length() const { return IR().size(); }
template<typename D, bool M> const PropertyKey& Ops<RootedIdVector, D, M>::operator[](size_t i) const { return IR()[i]; }
const char* InformalValueTypeName(const Value& v);
}
namespace js {
bool GetPropertyKeys(JSContext* cx, JS::HandleObject obj, unsigned flags, JS::RootedIdVector* props);
}
JSObject* JS_GetConstructor(JSContext* cx, JS::HandleObject proto);

// ---- GC parameters (QuickJS has no tunables for these; return runtime stats where possible) ----
enum JSGCParamKey { JSGC_MAX_BYTES, JSGC_BYTES, JSGC_NUMBER, JSGC_MODE, JSGC_MAX_MALLOC_BYTES, JSGC_SLICE_TIME_BUDGET_MS,
	JSGC_INCREMENTAL_GC_ENABLED, JSGC_PER_ZONE_GC_ENABLED, JSGC_COMPACTING_ENABLED };
uint32_t JS_GetGCParameter(JSContext* cx, JSGCParamKey key);
void JS_SetGCParameter(JSContext* cx, JSGCParamKey key, uint32_t value);

// ---- Classes and private data ----
bool JSShim_IsInstance(JSContext* cx, JSObject* obj, const JSClass* clasp);
void* JS_GetInstancePrivate(JSContext* cx, JS::HandleObject obj, const JSClass* clasp, JS::CallArgs* args);
void* JS_GetPrivate(JSObject* obj);
void JS_SetPrivate(JSObject* obj, void* data);
const JSClass* JS_GetClass(JSObject* obj);
bool JS_InstanceOf(JSContext* cx, JS::HandleObject obj, const JSClass* clasp, JS::CallArgs* args);
JSObject* JS_InitClass(JSContext* cx, JS::HandleObject obj, JS::HandleObject parent_proto, const JSClass* clasp,
	JSNative constructor, unsigned nargs, const JSPropertySpec* ps, const JSFunctionSpec* fs,
	const JSPropertySpec* static_ps, const JSFunctionSpec* static_fs);
JSObject* JS_GetClassObject(JSContext* cx, JSProtoKey key, JS::MutableHandleObject objp);

// ---- Script compilation / evaluation ----
namespace mozilla { struct Utf8Unit {}; }
namespace JS {
class CompileOptions {
public:
	const char* file = "(eval)"; unsigned line = 1;
	explicit CompileOptions(JSContext*) {}
	CompileOptions& setFileAndLine(const char* f, unsigned l) { file = f; line = l; return *this; }
	CompileOptions& setIsRunOnce(bool) { return *this; }
};
enum class SourceOwnership { Borrowed, TakeOwnership };
template<typename Unit> class SourceText {
	const char* p = nullptr; size_t n = 0;
public:
	bool init(JSContext*, const char* c, size_t len, SourceOwnership) { p = c; n = len; return true; }
	const char* data() const { return p; }
	size_t length() const { return n; }
};
bool Evaluate(JSContext* cx, const CompileOptions& opts, const SourceText<mozilla::Utf8Unit>& src, MutableHandleValue rval);
JSFunction* CompileFunction(JSContext* cx, RootedObjectVector& scopeChain, const CompileOptions& opts,
	const char* name, unsigned nargs, const char* const* argnames, const SourceText<mozilla::Utf8Unit>& src);
}
bool JS_CallFunction(JSContext* cx, JS::HandleObject thisObj, JS::HandleObject fun, const JS::HandleValueArray& args, JS::MutableHandleValue rval);
JSObject* JSShim_GlobalObject(JSContext* cx);
bool JSShim_GetOwnPropFlags(JSContext* cx, JSObject* obj, const char* name, bool* found, bool* writable, bool* configurable);
bool JSShim_EvalGlobalName(JSContext* cx, const char* name, JS::MutableHandleValue out);
void JSShim_SetGlobalCx(JSContext* cx);
// Request nesting (the temp pool is released when the outermost request ends).
void JSShim_RequestEnter();
void JSShim_RequestLeave(JSContext* cx);

JS::PropertyKey INT_TO_JSID(int i);

// ---- Structured clone (serialised with QuickJS's object writer) ----
struct JSStructuredCloneData { std::vector<uint8_t> buf; };

// ======== Batch: misc helpers, typed arrays, string chars, GC no-ops, hash map ========
#include <unordered_map>
#include <chrono>

namespace JS {
inline Value StringValue(JSString* s) { return Value(Conv<JSString*>::to(s)); }
inline void SetPrivate(JSObject* obj, void* data) { JS_SetPrivate(obj, data); }
inline const JSClass* GetClass(JSObject* obj) { return JS_GetClass(obj); }
struct AutoCheckCannotGC { AutoCheckCannotGC() {} AutoCheckCannotGC(JSContext*) {} };
using Latin1Char = unsigned char;

template<size_t N>
class RootedValueArray {
	Value vals[N];
public:
	explicit RootedValueArray(JSContext*) {}
	MutableHandleValue operator[](size_t i) { return MutableHandleValue(&vals[i]); }
	HandleValue operator[](size_t i) const { return HandleValue(vals[i]); }
	operator HandleValueArray() const { return HandleValueArray(N, vals); }
	const Value* begin() const { return vals; }
	size_t length() const { return N; }
};

class ConstUTF8CharsZ {
public:
	const char* p; size_t n;
	ConstUTF8CharsZ(const char* s, size_t len) : p(s), n(len) {}
};

// JS::GCHashMap replacement (QuickJS refcounts, so no tracing is required).
template<typename K, typename V, typename H, typename A>
class GCHashMap {
	std::unordered_map<JSObject*, V> m;
public:
	class Entry { public: JSObject* k; V* v; JSObject* key() const { return k; } V& value() const { return *v; } };
	class Ptr {
		friend class GCHashMap; Entry e; bool ok = false;
	public:
		explicit operator bool() const { return ok; }
		bool found() const { return ok; }
		const Entry* operator->() const { return &e; }
	};
	using AddPtr = Ptr;
	Ptr lookup(const K& key) {
		Ptr p; auto it = m.find(key.get());
		if (it != m.end()) { p.ok = true; p.e.k = it->first; p.e.v = &it->second; }
		return p;
	}
	bool put(const K& key, const V& v) { m[key.get()] = v; return true; }
	bool has(const K& key) const { return m.count(key.get()) > 0; }
	void clear() { m.clear(); }
	size_t count() const { return m.size(); }
	void trace(JSTracer*) {}
};

template<typename T> inline void TraceEdge(JSTracer*, T*, const char*) {}
} // namespace JS

namespace js {
struct SystemAllocPolicy {};
template<typename T> struct MovableCellHasher {};
namespace Scalar {
enum Type { Int8 = 0, Uint8, Int16, Uint16, Int32, Uint32, Float32, Float64, Uint8Clamped, BigInt64, BigUint64, MaxTypedArrayViewType };
}
}

enum JSType { JSTYPE_UNDEFINED, JSTYPE_OBJECT, JSTYPE_FUNCTION, JSTYPE_STRING, JSTYPE_NUMBER, JSTYPE_BOOLEAN,
	JSTYPE_NULL, JSTYPE_SYMBOL, JSTYPE_BIGINT, JSTYPE_LIMIT };
JSType JS_TypeOfValue(JSContext* cx, JS::HandleValue v);
int64_t JS_Now();

using JSTraceDataOp = void (*)(JSTracer*, void*);
inline bool JS_AddExtraGCRootsTracer(JSContext*, JSTraceDataOp, void*) { return true; }
inline void JS_RemoveExtraGCRootsTracer(JSContext*, JSTraceDataOp, void*) {}
inline JSObject* JS_GetFunctionObject(JSFunction* f) { return f; }

using JSInterruptCallback = bool (*)(JSContext*);
bool JS_AddInterruptCallback(JSContext* cx, JSInterruptCallback cb);

// Strings: report everything as two-byte so serialisation has a single format.
namespace JS { inline bool StringHasLatin1Chars(JSString*) { return false; } }
inline const JS::Latin1Char* JS_GetLatin1StringCharsAndLength(JSContext*, const JS::AutoCheckCannotGC&, JSString*, size_t*) { return nullptr; }
const char16_t* JS_GetTwoByteStringCharsAndLength(JSContext* cx, const JS::AutoCheckCannotGC&, JSString* str, size_t* length);
JSString* JS_NewStringCopyUTF8Z(JSContext* cx, const JS::ConstUTF8CharsZ& s);
inline JSString* JS_AtomizeAndPinJSString(JSContext*, JSString* s) { return s; }
JSString* JS_AtomizeAndPinUCStringN(JSContext* cx, const char16_t* chars, size_t length);

// Typed arrays / ArrayBuffers.
JSObject* JS_NewUint8Array(JSContext* cx, size_t length);
JSObject* JS_NewUint16Array(JSContext* cx, size_t length);
uint8_t* JS_GetUint8ArrayData(JSObject* obj, bool* isShared, const JS::AutoCheckCannotGC&);
uint16_t* JS_GetUint16ArrayData(JSObject* obj, bool* isShared, const JS::AutoCheckCannotGC&);
size_t JS_GetTypedArrayLength(JSObject* obj);
size_t JS_GetTypedArrayByteOffset(JSObject* obj);
js::Scalar::Type JS_GetArrayBufferViewType(JSObject* obj);
JSObject* JS_GetArrayBufferViewBuffer(JSContext* cx, JS::HandleObject obj, bool* isShared);
namespace JS {
bool IsArrayBufferObject(JSObject* obj);
uint8_t* GetArrayBufferData(JSObject* obj, bool* isShared, const AutoCheckCannotGC&);
size_t GetArrayBufferByteLength(JSObject* obj);
JSObject* NewArrayBufferWithContents(JSContext* cx, size_t nbytes, void* contents);
}
JSObject* JSShim_NewTypedArrayWithBuffer(JSContext* cx, int type, JS::HandleObject buffer, size_t byteOffset, int64_t length);
#define JSSHIM_TA(Name, TYPE) inline JSObject* JS_New##Name##ArrayWithBuffer(JSContext* cx, JS::HandleObject b, size_t off, int64_t len) { return JSShim_NewTypedArrayWithBuffer(cx, TYPE, b, off, len); }
JSSHIM_TA(Int8, JS_TYPED_ARRAY_INT8) JSSHIM_TA(Uint8, JS_TYPED_ARRAY_UINT8) JSSHIM_TA(Uint8Clamped, JS_TYPED_ARRAY_UINT8C)
JSSHIM_TA(Int16, JS_TYPED_ARRAY_INT16) JSSHIM_TA(Uint16, JS_TYPED_ARRAY_UINT16) JSSHIM_TA(Int32, JS_TYPED_ARRAY_INT32)
JSSHIM_TA(Uint32, JS_TYPED_ARRAY_UINT32) JSSHIM_TA(Float32, JS_TYPED_ARRAY_FLOAT32) JSSHIM_TA(Float64, JS_TYPED_ARRAY_FLOAT64)
#undef JSSHIM_TA

// ======== Proxy objects (GUI proxies) via QuickJS exotic classes ========
namespace mozilla {
template<typename T> class Maybe {
	bool has = false; T val{};
public:
	Maybe() {}
	bool isSome() const { return has; }
	bool isNothing() const { return !has; }
	T& ref() { return val; }
	const T& ref() const { return val; }
	T* operator->() { return &val; }
	const T* operator->() const { return &val; }
	void emplace(const T& v) { val = v; has = true; }
	void reset() { has = false; }
};
}
namespace JS {
struct PropertyDescriptor { Value value; unsigned attrs = 0; bool hasGetter_ = false, hasSetter_ = false; bool hasGetter() const { return hasGetter_; } bool hasSetter() const { return hasSetter_; } bool writable() const { return !(attrs & JSPROP_READONLY); } bool enumerable() const { return attrs & JSPROP_ENUMERATE; } bool configurable() const { return !(attrs & JSPROP_PERMANENT); } };
using MutableHandleIdVector = MutableHandle<RootedIdVector>;
}
// Error message ids (only the ones the engine references).
enum { JSMSG_OBJECT_REQUIRED = 1, JSMSG_BAD_PROP_ID, JSMSG_NOT_FUNCTION, JSMSG_USER_DEFINED_ERROR };

#define PROXY_CLASS_DEF(name, flags) { name, flags, nullptr }

namespace js {
struct ProxyOptions {
	const JSClass* clasp = nullptr;
	ProxyOptions& setClass(const JSClass* c) { clasp = c; return *this; }
};
class BaseProxyHandler {
public:
	BaseProxyHandler(const void*, bool, bool) {}
	virtual ~BaseProxyHandler() {}
	virtual bool get(JSContext*, JS::HandleObject, JS::HandleValue, JS::HandleId, JS::MutableHandleValue) const { return false; }
	virtual bool set(JSContext*, JS::HandleObject, JS::HandleId, JS::HandleValue, JS::HandleValue, JS::ObjectOpResult&) const { return false; }
	virtual bool delete_(JSContext*, JS::HandleObject, JS::HandleId, JS::ObjectOpResult&) const { return false; }
	virtual bool getOwnPropertyDescriptor(JSContext*, JS::HandleObject, JS::HandleId, JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>>) const { return true; }
	virtual bool defineProperty(JSContext*, JS::HandleObject, JS::HandleId, JS::Handle<JS::PropertyDescriptor>, JS::ObjectOpResult&) const { return false; }
	virtual bool ownPropertyKeys(JSContext*, JS::HandleObject, JS::MutableHandleIdVector) const { return true; }
	virtual bool enumerate(JSContext*, JS::HandleObject, JS::MutableHandleIdVector) const { return true; }
	virtual bool getPrototypeIfOrdinary(JSContext*, JS::HandleObject, bool*, JS::MutableHandleObject) const { return false; }
	virtual bool setImmutablePrototype(JSContext*, JS::HandleObject, bool*) const { return false; }
	virtual bool preventExtensions(JSContext*, JS::HandleObject, JS::ObjectOpResult&) const { return true; }
	virtual bool isExtensible(JSContext*, JS::HandleObject, bool*) const { return false; }
};
JSObject* NewProxyObject(JSContext* cx, const BaseProxyHandler* handler, JS::HandleValue priv, JSObject* proto, const ProxyOptions& options);
JS::Value GetProxyPrivate(const JSObject* obj);
JS::Value GetProxyReservedSlot(const JSObject* obj, size_t slot);
void SetProxyReservedSlot(JSObject* obj, size_t slot, const JS::Value& v);
}
namespace JS {
inline Value PrivateValue(void* p) { Value v; v.setPrivate(p); return v; }
}

// ======== Collections, iteration, promises ========
namespace JS {
bool IsArray(JSContext* cx, HandleObject obj, bool* isArray);
bool IsMapObject(JSContext* cx, HandleObject obj, bool* isMap);
bool IsSetObject(JSContext* cx, HandleObject obj, bool* isSet);
uint32_t MapSize(JSContext* cx, HandleObject obj);
uint32_t SetSize(JSContext* cx, HandleObject obj);
bool MapEntries(JSContext* cx, HandleObject obj, MutableHandleValue rval);
bool SetValues(JSContext* cx, HandleObject obj, MutableHandleValue rval);
JSObject* NewMapObject(JSContext* cx);
JSObject* NewSetObject(JSContext* cx);
bool MapSet(JSContext* cx, HandleObject obj, HandleValue key, HandleValue val);
bool SetAdd(JSContext* cx, HandleObject obj, HandleValue key);

class ForOfIterator {
	JSContext* cx; Value iterator;
public:
	explicit ForOfIterator(JSContext* c) : cx(c) {}
	enum NonIterableBehavior { ThrowOnNonIterable, AllowNonIterable };
	bool init(HandleValue iterable, NonIterableBehavior = ThrowOnNonIterable);
	bool next(MutableHandleValue val, bool* done);
};

JSObject* NewPromiseObject(JSContext* cx, HandleObject executor);
bool ResolvePromise(JSContext* cx, HandleObject promise, HandleValue resolution);
bool RejectPromise(JSContext* cx, HandleObject promise, HandleValue rejection);
}

bool JS_Enumerate(JSContext* cx, JS::HandleObject obj, JS::MutableHandle<JS::IdVector> props);
bool JS_GetPropertyDescriptorById(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
	JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> desc, JS::MutableHandleObject holder);

JSFunction* JS_ValueToFunction(JSContext* cx, JS::HandleValue v);
JSString* JS_GetFunctionId(JSFunction* fun);

inline bool JS_DoubleIsInt32(double d, int32_t* out) { int32_t i = (int32_t)d; if ((double)i == d && !(i == 0 && std::signbit(d))) { *out = i; return true; } return false; }

// SpiderMonkey-only globals (uneval, toSource) that the game scripts use.
void JSShim_InstallPrelude(JSContext* cx);
