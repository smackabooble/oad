// Part 2 of the shim: handles, rooted types, owning heap types. Included from jsapi.h.
#pragma once
#include <type_traits>
namespace mozilla { template<typename T> class Maybe; }
namespace JS {
template<typename T> struct IsMaybe : std::false_type {};
template<typename T> struct IsMaybe<mozilla::Maybe<T>> : std::true_type {};

template<typename T> struct Conv;
template<> struct Conv<Value> {
	static JSValue to(const Value& v) { return v.raw(); }
};
template<> struct Conv<JSObject*> {
	static JSValue to(JSObject* o) { return o ? JS_MKPTR(JS_TAG_OBJECT, o) : JS_NULL; }
	static JSObject* from(JSValue v) { return JS_IsObject(v) ? (JSObject*)JS_VALUE_GET_PTR(v) : nullptr; }
};
template<> struct Conv<JSString*> {
	static JSValue to(JSString* o) { return o ? JS_MKPTR(JS_TAG_STRING, o) : JS_NULL; }
	static JSString* from(JSValue v) { return JS_IsString(v) ? (JSString*)JS_VALUE_GET_PTR(v) : nullptr; }
};

// jsid / PropertyKey: a QuickJS atom.
struct PropertyKey {
	JSAtom atom = 0;
	PropertyKey() {}
	explicit PropertyKey(JSAtom a) : atom(a) {}
	static PropertyKey Int(int i);
	static PropertyKey fromPinnedString(JSString* s);
};
template<> struct Conv<PropertyKey> {
	static JSValue to(const PropertyKey&) { return JS_UNDEFINED; } // atoms are not refcounted JSValues
};

template<typename T> class MutableHandle;
template<typename T> class Rooted;
template<typename T> class Heap;
template<typename T> class PersistentRooted;

// Forwarding of Value methods through Handle/Rooted/Heap wrappers (as WrappedPtrOperations in SM).
#define JSSHIM_VALUE_READ_OPS \
	bool isUndefined() const { return VR().isUndefined(); } \
	bool isNull() const { return VR().isNull(); } \
	bool isNullOrUndefined() const { return VR().isNullOrUndefined(); } \
	bool isObject() const { return VR().isObject(); } \
	bool isObjectOrNull() const { return VR().isObjectOrNull(); } \
	bool isString() const { return VR().isString(); } \
	bool isBoolean() const { return VR().isBoolean(); } \
	bool isInt32() const { return VR().isInt32(); } \
	bool isDouble() const { return VR().isDouble(); } \
	bool isNumber() const { return VR().isNumber(); } \
	bool isPrimitive() const { return VR().isPrimitive(); } \
	bool isSymbol() const { return VR().isSymbol(); } \
	bool toBoolean() const { return VR().toBoolean(); } \
	int32_t toInt32() const { return VR().toInt32(); } \
	double toDouble() const { return VR().toDouble(); } \
	double toNumber() const { return VR().toNumber(); } \
	JSObject* toObjectOrNull() const { return VR().toObjectOrNull(); } \
	JSObject& toObject() const { return VR().toObject(); } \
	JSString* toString() const { return Conv<JSString*>::from(VR().raw()); }
#define JSSHIM_VALUE_WRITE_OPS \
	void setUndefined() const { VW() = UndefinedValue(); } \
	void setNull() const { VW() = NullValue(); } \
	void setBoolean(bool b) const { VW() = BooleanValue(b); } \
	void setInt32(int32_t i) const { VW() = Int32Value(i); } \
	void setNumber(double d) const { VW() = NumberValue(d); } \
	void setDouble(double d) const { VW() = DoubleValue(d); } \
	void setObject(JSObject& o) const { VW() = ObjectValue(o); } \
	void setObjectOrNull(JSObject* o) const { VW() = ObjectOrNullValue(o); } \
	void setString(JSString* str) const { VW() = Value(Conv<JSString*>::to(str)); }

template<typename T, typename D, bool Mut> struct Ops {};
class RootedIdVector;
template<typename D, bool Mut> struct Ops<RootedIdVector, D, Mut> {
	const RootedIdVector& IR() const { return static_cast<const D*>(this)->get(); }
	size_t length() const;
	const struct PropertyKey& operator[](size_t i) const;
};
template<typename D> struct Ops<Value, D, false> {
	const Value& VR() const { return static_cast<const D*>(this)->get(); }
	JSSHIM_VALUE_READ_OPS
};
template<typename D> struct Ops<Value, D, true> {
	const Value& VR() const { return static_cast<const D*>(this)->get(); }
	Value& VW() const { return const_cast<Value&>(static_cast<const D*>(this)->get()); }
	JSSHIM_VALUE_READ_OPS
	JSSHIM_VALUE_WRITE_OPS
};

template<typename T>
class Handle : public Ops<T, Handle<T>, false> {
	const T* p;
public:
	constexpr Handle(const T& ref) : p(&ref) {}
	Handle(const Rooted<T>& r) : p(&r.get()) {}
	Handle(const MutableHandle<T>& m) : p(&m.get()) {}
	Handle(const Heap<T>& h) : p(h.address()) {}
	Handle(const PersistentRooted<T>& r) : p(&r.get()) {}
	const T& get() const { return *p; }
	operator const T&() const { return *p; }
	decltype(auto) operator*() const { if constexpr (std::is_pointer_v<T>) return **p; else return (*p); }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return *p; else return p; }
	const T* address() const { return p; }
	static Handle fromMarkedLocation(const T* ptr) { return Handle(*ptr); }
};

template<typename T>
class MutableHandle : public Ops<T, MutableHandle<T>, true> {
	T* p;
public:
	MutableHandle(T* ptr) : p(ptr) {}
	MutableHandle(Rooted<T>* r) : p(&r->get()) {}
	MutableHandle(PersistentRooted<T>* r) : p(r->operator&().address()) {}
	T& get() const { return *p; }
	operator T&() const { return *p; }
	T& operator*() const { return *p; }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return *p; else return p; }
	T* address() const { return p; }
	void set(const T& v) const { *p = v; }
};

// Rooted: plain storage. Liveness is guaranteed by the context's temp pool, so no
// per-instance refcounting happens here.
template<typename T>
class Rooted : public Ops<T, Rooted<T>, true> {
	T v;
public:
	explicit Rooted(JSContext*) : v() {}
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<const U&, T>>>
	Rooted(JSContext*, const U& init) : v(init) {}
	Rooted(const Rooted&) = delete;
	T& get() { return v; }
	const T& get() const { return v; }
	T* address() { return &v; }
	const T* address() const { return &v; }
	operator const T&() const { return v; }
	void set(const T& x) { v = x; }
	Rooted& operator=(const T& x) { v = x; return *this; }
	Rooted& operator=(const Handle<T>& x) { v = x.get(); return *this; }
	decltype(auto) operator*() const { if constexpr (std::is_pointer_v<T>) return *v; else return (v); }
	MutableHandle<T> operator&() { return MutableHandle<T>(&v); }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return v; else if constexpr (IsMaybe<T>::value) return &v.ref(); else return &v; }
	auto operator->() { if constexpr (std::is_pointer_v<T>) return v; else if constexpr (IsMaybe<T>::value) return &v.ref(); else return &v; }
	bool isSome() const { if constexpr (IsMaybe<T>::value) return v.isSome(); else return false; }
};

// Owning variants hold a real refcount so they can outlive the request.
JSRuntime* JSShim_GlobalRt();
void JSShim_DeferFree(JSRuntime* rt, JSValue v);

template<typename T>
class Heap : public Ops<T, Heap<T>, false> {
	T v{};
	JSRuntime* rt = nullptr;
	static JSValue J(const T& x) { return Conv<T>::to(x); }
	void acquire() { rt = JSShim_GlobalRt(); if (rt) JS_DupValueRT(rt, J(v)); }
	// Dropped references are freed when the outermost script request ends, not immediately: game code often
	// copies a value out of a Heap/queue into a Rooted and then removes the queue entry (SpiderMonkey's GC kept
	// the Rooted copy alive; here nothing else would).
	void release() { if (rt) { JSShim_DeferFree(rt, J(v)); rt = nullptr; } }
public:
	Heap() {}
	explicit Heap(const T& x) : v(x) { acquire(); }
	Heap(const Heap& o) : v(o.v) { acquire(); }
	~Heap() { release(); }
	// Take the new reference before dropping the old one: the new value may be the very object this Heap
	// is the last owner of (self-assignment through scratch storage), which release() would free.
	Heap& operator=(const T& x) {
		const T nv = x; JSRuntime* nrt = JSShim_GlobalRt();
		if (nrt) JS_DupValueRT(nrt, J(nv));
		release(); v = nv; rt = nrt; return *this;
	}
	Heap& operator=(const Heap& o) { if (this != &o) { *this = o.v; } return *this; }
	void set(const T& x) { *this = x; }
	const T& get() const { return v; }
	operator const T&() const { return v; }
	const T* address() const { return &v; }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return v; else return &v; }
	void init(const T& x) { *this = x; }
};

// Writes through `&persistent` land in scratch storage; they are turned into real references
// (a) the next time the object is read and (b) before the temp pool is released.
struct PersistentSyncBase { virtual void sync() = 0; virtual void retire() = 0; bool registered_ = false; unsigned gen_ = 0; virtual ~PersistentSyncBase() {} };
unsigned JSShim_DirtyGen();
void JSShim_RegisterDirty(PersistentSyncBase*);
void JSShim_UnregisterDirty(PersistentSyncBase*);

template<typename T>
class PersistentRooted : public Ops<T, PersistentRooted<T>, true>, public PersistentSyncBase {
	Heap<T> h;
	T tmp{};
	bool dirty = false;
public:
	PersistentRooted() {}
	explicit PersistentRooted(JSContext*) {}
	PersistentRooted(const PersistentRooted& o) : h(o.get()) {}
	~PersistentRooted() { if (registered_) JSShim_UnregisterDirty(this); }
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<const U&, T>>>
	PersistentRooted(JSContext*, const U& x) : h(static_cast<const T&>(x)) {}
	void init(JSContext*) {}
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<const U&, T>>>
	void init(JSContext*, const U& x) { dirty = false; h = static_cast<const T&>(x); }
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<const U&, T>>>
	PersistentRooted& operator=(const U& x) { dirty = false; h = static_cast<const T&>(x); return *this; }
	PersistentRooted& operator=(const PersistentRooted& o) { dirty = false; h = o.get(); return *this; }
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<const U&, T>>>
	void set(const U& x) { dirty = false; h = static_cast<const T&>(x); }
	// sync() copies scratch -> real reference but keeps `dirty`, so a write through an earlier `&persistent` handle that
	// happens after a (premature) sync is not lost; retire() ends the scratch phase (see JSShim_PoolRelease).
	void sync() override { if (dirty && !(tmp == h.get())) h = tmp; }
	void retire() override { dirty = false; }
	MutableHandle<T> operator&() {
		if (!dirty) { tmp = h.get(); dirty = true; }
		gen_ = JSShim_DirtyGen();
		if (!registered_) JSShim_RegisterDirty(this);
		return MutableHandle<T>(&tmp);
	}
	const T& get() const { const_cast<PersistentRooted*>(this)->sync(); return h.get(); }
	const T* address() const { return &get(); }
	operator const T&() const { return get(); }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return get(); else return &get(); }
	bool initialized() const { return true; }
	void reset() { dirty = false; h = T(); }
};

using RootedValue = Rooted<Value>;
using RootedObject = Rooted<JSObject*>;
using RootedString = Rooted<JSString*>;
using RootedId = Rooted<PropertyKey>;
using HandleValue = Handle<Value>;
using HandleObject = Handle<JSObject*>;
using HandleString = Handle<JSString*>;
using HandleId = Handle<PropertyKey>;
using MutableHandleValue = MutableHandle<Value>;
using MutableHandleObject = MutableHandle<JSObject*>;
using MutableHandleString = MutableHandle<JSString*>;
using MutableHandleId = MutableHandle<PropertyKey>;
using PersistentRootedValue = PersistentRooted<Value>;
using PersistentRootedObject = PersistentRooted<JSObject*>;

extern const Value UndefinedHandleValueStorage, NullHandleValueStorage, TrueHandleValueStorage, FalseHandleValueStorage;
inline const HandleValue UndefinedHandleValue{UndefinedHandleValueStorage};
inline const HandleValue NullHandleValue{NullHandleValueStorage};
inline const HandleValue TrueHandleValue{TrueHandleValueStorage};
inline const HandleValue FalseHandleValue{FalseHandleValueStorage};

} // namespace JS
