// Part 2 of the shim: handles, rooted types, owning heap types. Included from jsapi.h.
#pragma once
#include <type_traits>
namespace JS {

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
};
template<> struct Conv<PropertyKey> {
	static JSValue to(const PropertyKey&) { return JS_UNDEFINED; } // atoms are not refcounted JSValues
};

template<typename T> class MutableHandle;
template<typename T> class Rooted;

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
	Handle(const T& ref) : p(&ref) {}
	Handle(const Rooted<T>& r) : p(&r.get()) {}
	Handle(const MutableHandle<T>& m) : p(&m.get()) {}
	const T& get() const { return *p; }
	operator const T&() const { return *p; }
	decltype(auto) operator*() const { if constexpr (std::is_pointer_v<T>) return **p; else return (*p); }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return *p; else return p; }
	const T* address() const { return p; }
};

template<typename T>
class MutableHandle : public Ops<T, MutableHandle<T>, true> {
	T* p;
public:
	MutableHandle(T* ptr) : p(ptr) {}
	MutableHandle(Rooted<T>* r) : p(&r->get()) {}
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
	Rooted(JSContext*, const T& init) : v(init) {}
	Rooted(JSContext*, const Handle<T>& init) : v(init.get()) {}
	Rooted(JSContext*, const MutableHandle<T>& init) : v(init.get()) {}
	Rooted(const Rooted&) = delete;
	T& get() { return v; }
	const T& get() const { return v; }
	T* address() { return &v; }
	const T* address() const { return &v; }
	operator const T&() const { return v; }
	Rooted& operator=(const T& x) { v = x; return *this; }
	Rooted& operator=(const Handle<T>& x) { v = x.get(); return *this; }
	decltype(auto) operator*() const { if constexpr (std::is_pointer_v<T>) return *v; else return (v); }
	MutableHandle<T> operator&() { return MutableHandle<T>(&v); }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return v; else return &v; }
};

// Owning variants hold a real refcount so they can outlive the request.
JSContext* JSShim_GlobalCx();

template<typename T>
class Heap : public Ops<T, Heap<T>, false> {
	T v{};
	JSContext* cx = nullptr;
	static JSValue J(const T& x) { return Conv<T>::to(x); }
	void acquire() { cx = JSShim_GlobalCx(); if (cx) JS_DupValue(cx, J(v)); }
	void release() { if (cx) { JS_FreeValue(cx, J(v)); cx = nullptr; } }
public:
	Heap() {}
	explicit Heap(const T& x) : v(x) { acquire(); }
	Heap(const Heap& o) : v(o.v) { acquire(); }
	~Heap() { release(); }
	Heap& operator=(const T& x) { release(); v = x; acquire(); return *this; }
	Heap& operator=(const Heap& o) { if (this != &o) { release(); v = o.v; acquire(); } return *this; }
	void set(const T& x) { *this = x; }
	const T& get() const { return v; }
	operator const T&() const { return v; }
	const T* address() const { return &v; }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return v; else return &v; }
	void init(const T& x) { *this = x; }
};

template<typename T>
class PersistentRooted : public Ops<T, PersistentRooted<T>, true> {
	Heap<T> h;
public:
	PersistentRooted() {}
	explicit PersistentRooted(JSContext*) {}
	PersistentRooted(JSContext*, const T& x) : h(x) {}
	PersistentRooted(JSContext*, const Handle<T>& x) : h(x.get()) {}
	void init(JSContext*) {}
	void init(JSContext*, const T& x) { h = x; }
	void init(JSContext*, const Handle<T>& x) { h = x.get(); }
	PersistentRooted& operator=(const T& x) { h = x; return *this; }
	PersistentRooted& operator=(const Handle<T>& x) { h = x.get(); return *this; }
	const T& get() const { return h.get(); }
	operator const T&() const { return h.get(); }
	auto operator->() const { if constexpr (std::is_pointer_v<T>) return h.get(); else return &h.get(); }
	bool initialized() const { return true; }
	void reset() { h = T(); }
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
using MutableHandleId = MutableHandle<PropertyKey>;
using PersistentRootedValue = PersistentRooted<Value>;
using PersistentRootedObject = PersistentRooted<JSObject*>;

extern const Value UndefinedHandleValueStorage, NullHandleValueStorage, TrueHandleValueStorage, FalseHandleValueStorage;
inline HandleValue UndefinedHandleValue() { return HandleValue(UndefinedHandleValueStorage); }

} // namespace JS
