#pragma once
#include <random>

namespace vm
{
	class vec2;
	class vec3;
	class vec4;	
	class mat4;
	class quat;

	using cfloat = const float;
	using cint = const int;
	using cvec2 = const vec2;
	using cvec3 = const vec3;
	using cvec4 = const vec4;
	using cmat4 = const mat4;
	using cquat = const quat;
	using col = vec4;
	using row = vec4;
	using ccol = const col;

	class vec2
	{
	public:
		vec2();
		vec2(cfloat value);
		vec2(cfloat x, cfloat y);
		vec2(cvec2& v);
		vec2(cfloat* v);
		vec2(cvec2* v);
		void operator=(cvec2& v);
		vec2 operator+(cvec2& v) const;
		vec2 operator-() const;
		vec2 operator-(cvec2& v) const;
		vec2 operator*(cvec2& v) const;
		vec2 operator*(cfloat scalar) const;
		vec2 operator/(cvec2& v) const;
		vec2 operator/(cfloat scalar) const;
		void operator+=(cvec2& v);
		void operator-=(cvec2& v);
		void operator*=(cvec2& v);
		void operator*=(cfloat scalar);
		void operator/=(cvec2& v);
		void operator/=(cfloat scalar);
		bool operator==(cfloat* v) const;
		bool operator==(cvec2* v) const;
		bool operator==(cvec2& v) const;
		bool operator!=(cfloat* v) const;
		bool operator!=(cvec2* v) const;
		bool operator!=(cvec2& v) const;
		float& operator[](unsigned i);
		float* ptr();

		float x, y;
	};

	class vec3
	{
	public:
		vec3();
		vec3(cfloat value);
		vec3(cfloat x, cfloat y, cfloat z);
		vec3(cvec2& v, cfloat z);
		vec3(cvec3& v);
		vec3(cvec4& v);
		vec3(cfloat* v);
		vec3(cvec3* v);
		void operator=(cvec3& v);
		vec3 operator+(cvec3& v) const;
		vec3 operator-() const;
		vec3 operator-(cvec3& v) const;
		vec3 operator*(cvec3& v) const;
		vec3 operator*(cfloat scalar) const;
		vec3 operator/(cvec3& v) const;
		vec3 operator/(cfloat scalar) const;
		void operator+=(cvec3& v);
		void operator-=(cvec3& v);
		void operator*=(cvec3& v);
		void operator*=(cfloat scalar);
		void operator/=(cvec3& v);
		void operator/=(cfloat scalar);
		bool operator==(cfloat* v) const;
		bool operator==(cvec3* v) const;
		bool operator==(cvec3& v) const;
		bool operator!=(cfloat* v) const;
		bool operator!=(cvec3* v) const;
		bool operator!=(cvec3& v) const;
		float& operator[](unsigned i);
		float* ptr();

		float x, y, z;
	};
	
	class ivec4
	{
	public:
		ivec4() : x(0), y(0) ,z(0), w(0) {}
		ivec4(const int* v) { x = v[0]; y = v[1]; z = v[2]; w = v[3]; }
		int& operator[](unsigned i) {
			return (&x)[i];
		}
		int x, y, z, w;
	};

	class vec4
	{
	public:
		vec4();
		vec4(cfloat value);
		vec4(cfloat x, cfloat y, cfloat z, cfloat w);
		vec4(cvec3& v, cfloat w);
		vec4(cvec4& v);
		vec4(cfloat* v);
		vec4(cvec4* v);
		void operator=(cvec4& v);
		vec4 operator+(cvec4& v) const;
		vec4 operator-() const;
		vec4 operator-(cvec4& v) const;
		vec4 operator*(cvec4& v) const;
		vec4 operator*(cfloat scalar) const;
		vec4 operator/(cvec4& v) const;
		vec4 operator/(cfloat scalar) const;
		void operator+=(cvec4& v);
		void operator-=(cvec4& v);
		void operator*=(cvec4& v);
		void operator*=(cfloat scalar);
		void operator/=(cvec4& v);
		void operator/=(cfloat scalar);
		bool operator==(cfloat* v) const;
		bool operator==(cvec4* v) const;
		bool operator==(cvec4& v) const;
		bool operator!=(cfloat* v) const;
		bool operator!=(cvec4* v) const;
		bool operator!=(cvec4& v) const;
		float& operator[](unsigned i);
		float* ptr();

		float x, y, z, w;
	};

	class mat4
	{
	public:
		mat4();
		mat4(cfloat diagonal);
		mat4(cfloat* m);
		mat4(cmat4* m);
		mat4(cmat4& m);
		mat4(ccol& v0, ccol& v1, ccol& v2, ccol& v3);
		mat4(cquat& q);
		mat4(cfloat& x0, cfloat& y0, cfloat& z0, cfloat& w0,
			cfloat& x1, cfloat& y1, cfloat& z1, cfloat& w1,
			cfloat& x2, cfloat& y2, cfloat& z2, cfloat& w2,
			cfloat& x3, cfloat& y3, cfloat& z3, cfloat& w3);
		static cmat4 identity();
		quat quaternion() const;
		vec3 eulerAngles() const;
		float pitch() const;
		float yaw() const;
		float roll() const;
		vec3 translation() const;
		vec3 scale() const;
		quat rotation() const;
		void operator=(cmat4& m);
		mat4 operator*(cmat4& m) const;
		vec4 operator*(cvec4& v) const;
		mat4 operator*(cfloat scalar) const;
		bool operator==(cfloat* m) const;
		bool operator==(cmat4* m) const;
		bool operator==(cmat4& m) const;
		bool operator!=(cfloat* m) const;
		bool operator!=(cmat4* m) const;
		bool operator!=(cmat4& m) const;
		col& operator[](unsigned i);
		float* ptr();

		col _v[4];
	};

	class quat
	{
	public:
		quat();
		quat(cfloat* q);
		quat(cquat* q);
		quat(cquat& q);
		quat(cfloat f, cvec3& v);
		quat(cfloat w, cfloat x, cfloat y, cfloat z);
		quat(cvec3& u, cvec3& v);
		quat(cvec3& eulerAngle);
		quat(cvec4& eulerAngle);
		quat(cmat4& m);
		static cquat identity();
		mat4 matrix() const;
		vec3 eulerAngles() const;
		float pitch() const;
		float yaw() const;
		float roll() const;
		void operator=(cquat& q);
		quat operator+(cquat& q) const;
		quat operator-(cquat& q) const;
		quat operator-() const;
		quat operator*(cfloat scalar) const;
		vec3 operator*(cvec3& v) const;
		vec4 operator*(cvec4& v) const;
		quat operator*(cquat& q) const;
		quat operator/(cfloat scalar) const;
		bool operator==(cquat& q) const;
		bool operator!=(cquat& q) const;

		float& operator[](unsigned i);
		float* ptr();

		float x, y, z, w;
	};

	class Ray
	{
	public:
		vec3 o, d; // origin, direction
		Ray(cvec3& o, cvec3& d);
	};

	class Transform
	{
	public:
		Transform();
		Transform(cvec3& _scale, cquat& _rotation, cvec3& _position);
		mat4 matrix() const;

	private:
		vec3 m_scale;
		quat m_rotation;
		vec3 m_position;
	};

	vec2 operator*(cfloat scalar, cvec2& v);
	vec3 operator*(cfloat scalar, cvec3& v);
	vec4 operator*(cfloat scalar, cvec4& v);
	quat operator*(cfloat scalar, cquat& q);
	vec3 operator*(cvec3& v, cquat& q);
	vec4 operator*(cvec4& v, cquat& q);
	mat4 inverse(cmat4& m);
	quat inverse(cquat& q);
	quat conjugate(cquat& q);
	mat4 transpose(cmat4& m);
	mat4 translate(cmat4& m, cvec3& v);
	mat4 scale(cmat4& m, cvec3& v);
	mat4 rotate(cmat4& m, cfloat angle, cvec3& axis);
	quat rotate(cquat& q, cfloat angle, cvec3& axis);
	mat4 transform(cquat& r, cvec3& s, cvec3& t);
	mat4 perspective(cfloat fovy, cfloat aspect, cfloat zNear, cfloat zFar);
	mat4 ortho(cfloat left, cfloat right, cfloat bottom, cfloat top, cfloat zNear, cfloat zFar);
	mat4 lookAt(cvec3& eye, cvec3& front, cvec3& right, cvec3& up);
	quat lookAt(cvec3& front, cvec3& right, cvec3& up);
	float length(cvec2& v);
	float length(cvec3& v);
	float length(cvec4& v);
	float length(cquat& q);
	float lengthSquared(cvec2& v);
	float lengthSquared(cvec3& v);
	float lengthSquared(cvec4& v);
	float lengthSquared(cquat& q);
	float dot(cvec2& v1, cvec2& v2);
	float dot(cvec3& v1, cvec3& v2);
	float dot(cvec4& v1, cvec4& v2);
	float dot(cquat& q1, cquat& q2);
	vec2 normalize(cvec2& v);
	vec3 normalize(cvec3& v);
	vec4 normalize(cvec4& v);
	quat normalize(cquat& q);
	vec3 cross(cvec3& v1, cvec3& v2);
	quat cross(cquat& q1, cquat& q2);
	float inversesqrt(cfloat x);
	float radians(cfloat degrees);
	float degrees(cfloat radians);
	vec3 radians(cvec3& v);
	vec3 degrees(cvec3& v);
	vec3 reflect(cvec3& v, cvec3& normal);
	float mix(cfloat f1, cfloat f2, cfloat a);
	vec4 mix(cvec4& v1, cvec4& v2, cfloat a);
	quat mix(cquat& q1, cquat& q2, cfloat a);
	quat lerp(cquat& q1, cquat& q2, cfloat a);
	quat slerp(cquat& q1, cquat& q2, cfloat a);
	vec3 minimum(cvec3& v1, cvec3& v2);
	vec3 maximum(cvec3& v1, cvec3& v2);
	template<typename T> inline T minimum(const T& a, const T& b) { return (b < a) ? b : a; };
	template<typename T> inline T maximum(const T& a, const T& b) { return (a < b) ? b : a; };
	template<typename T> inline T clamp(const T& x, const T& minX, const T& maxX) { return minimum(maximum(x, minX), maxX); };
	template<typename T> inline void clamp(T* const x, const T& minX, const T& maxX) { *x = clamp(*x, minX, maxX); };
	float rand(cfloat a, cfloat b);
	float lerp(cfloat a, cfloat b, cfloat f);
	float halton(uint32_t index, uint32_t base);
	vec2 halton_2_3(uint32_t index);
	vec2 halton_2_3_next(uint32_t samples = 16);
}