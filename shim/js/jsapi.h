// SpiderMonkey-91-compatible API subset implemented on QuickJS (quickjs-ng).
// Design: JS::Value is a raw (unowned) JSValue. Rooted/Persistent* hold counted refs.
// Anything freshly created by a JS_New*/JS::New* call is put in a per-context
// temp pool (see JSShim_PoolRelease) so callers never manage refcounts manually.
#pragma once
extern "C" {
#include "quickjs.h"
}
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

struct JSObject;   // opaque: really the QuickJS object pointer
struct JSString;   // opaque
struct JSShimCtx { std::vector<JSValue> pool; };
JSShimCtx& JSShim_Ctx(JSContext* cx);
inline JSValue JSShim_Pool(JSContext* cx, JSValue v) { JSShim_Ctx(cx).pool.push_back(v); return v; }
void JSShim_PoolRelease(JSContext* cx, size_t keep = 0);

namespace JS {

inline JSValue ShimNum(double d) { int32_t i = (int32_t)d; return ((double)i == d && !(i == 0 && std::signbit(d))) ? JS_MKVAL(JS_TAG_INT, i) : JS_NewFloat64(nullptr, d); }

class Value {
	JSValue v;
public:
	Value() : v(JS_UNDEFINED) {}
	explicit Value(JSValue x) : v(x) {}
	JSValue raw() const { return v; }
	bool isUndefined() const { return JS_IsUndefined(v); }
	bool isNull() const { return JS_IsNull(v); }
	bool isNullOrUndefined() const { return isNull() || isUndefined(); }
	bool isObject() const { return JS_IsObject(v); }
	bool isObjectOrNull() const { return isObject() || isNull(); }
	bool isString() const { return JS_IsString(v); }
	bool isBoolean() const { return JS_IsBool(v); }
	bool isInt32() const { return JS_VALUE_GET_NORM_TAG(v) == JS_TAG_INT; }
	bool isDouble() const { return JS_IsNumber(v) && !isInt32(); }
	bool isNumber() const { return isInt32() || isDouble(); }
	bool isPrimitive() const { return !isObject(); }
	bool isSymbol() const { return JS_IsSymbol(v); }
	bool toBoolean() const { return JS_VALUE_GET_BOOL(v); }
	int32_t toInt32() const { return isInt32() ? JS_VALUE_GET_INT(v) : (int32_t)toDouble(); }
	double toDouble() const { return isInt32() ? (double)JS_VALUE_GET_INT(v) : JS_VALUE_GET_FLOAT64(v); }
	double toNumber() const { return toDouble(); }
	JSObject* toObjectOrNull() const { return isObject() ? (JSObject*)JS_VALUE_GET_PTR(v) : nullptr; }
	JSObject& toObject() const { return *(JSObject*)JS_VALUE_GET_PTR(v); }
	void setUndefined() { v = JS_UNDEFINED; }
	void setNull() { v = JS_NULL; }
	void setBoolean(bool b) { v = JS_NewBool(nullptr, b); }
	void setInt32(int32_t i) { v = JS_MKVAL(JS_TAG_INT, i); }
	void setDouble(double d) { v = JS_NewFloat64(nullptr, d); }
	void setNumber(double d) { v = ShimNum(d); }
	void setObject(JSObject& o) { v = JS_MKPTR(JS_TAG_OBJECT, &o); }
	void setObjectOrNull(JSObject* o) { v = o ? JS_MKPTR(JS_TAG_OBJECT, o) : JS_NULL; }
	void set(const Value& o) { v = o.v; }
	void setRaw(JSValue x) { v = x; }
	friend bool operator==(const Value& a, const Value& b) { return memcmp(&a.v, &b.v, sizeof(JSValue)) == 0; }
	friend bool operator!=(const Value& a, const Value& b) { return !(a == b); }
	// Private pointers are stored as tagged ints (wasm32: pointers are 32 bits).
	void setPrivate(void* p) { static_assert(sizeof(void*) == 4, "wasm32 only"); v = JS_MKVAL(JS_TAG_INT, (int32_t)(uintptr_t)p); }
	void* toPrivate() const { return (void*)(uintptr_t)(uint32_t)JS_VALUE_GET_INT(v); }
};

inline Value UndefinedValue() { return Value(JS_UNDEFINED); }
inline Value NaNValue() { return Value(JS_NAN); }
inline Value NullValue() { return Value(JS_NULL); }
inline Value BooleanValue(bool b) { return Value(JS_NewBool(nullptr, b)); }
inline Value Int32Value(int32_t i) { return Value(JS_MKVAL(JS_TAG_INT, i)); }
inline Value DoubleValue(double d) { return Value(JS_NewFloat64(nullptr, d)); }
inline Value NumberValue(double d) { return Value(ShimNum(d)); }
inline Value ObjectValue(JSObject& o) { return Value(JS_MKPTR(JS_TAG_OBJECT, &o)); }
inline Value ObjectOrNullValue(JSObject* o) { return o ? ObjectValue(*o) : NullValue(); }

} // namespace JS
#include "js/rooting.h"
#include "js/api.h"
