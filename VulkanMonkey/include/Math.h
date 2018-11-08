#pragma once
#include <cmath>

#define cfloat const float
#define cvec2  const vec2
#define cvec3  const vec3
#define cvec4  const vec4
#define cquat  const quat
#define ccol   const col
#define cmat4  const mat4
#define FLT_EPSILON 1.192092896e-07F
#define VM_LEFT_HANDED

// Left Handed, Colomn Major, Depth[0,1]
namespace vm
{
	struct vec2 {
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

	struct vec3 {
		vec3();
		vec3(cfloat value);
		vec3(cfloat x, cfloat y, cfloat z);
		vec3(cvec2& v, cfloat z);
		vec3(cvec3& v);
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
	struct vec4 {
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

	typedef vec4 col;
	struct mat4 {
		mat4();
		mat4(cfloat diagonal);
		mat4(cfloat* m);
		mat4(cmat4* m);
		mat4(cmat4& m);
		mat4(ccol& v0, ccol& v1, ccol& v2, ccol& v3);
		mat4(cfloat& x0, cfloat& y0, cfloat& z0, cfloat& w0,
			cfloat& x1, cfloat& y1, cfloat& z1, cfloat& w1,
			cfloat& x2, cfloat& y2, cfloat& z2, cfloat& w2,
			cfloat& x3, cfloat& y3, cfloat& z3, cfloat& w3);
		static const mat4& identity();
		void operator=(cmat4& m);
		mat4 operator*(cmat4& m) const;
		vec4 operator*(cvec4& m) const;
		mat4 operator*(cfloat scalar);
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

	struct quat {
		quat();
		quat(float* q);
		quat(cquat* q);
		quat(cquat& q);
		quat(cfloat f, cvec3& v);
		quat(cfloat w, cfloat x, cfloat y, cfloat z);
		quat(cvec3& u, cvec3& v);
		quat(cvec3& eulerAngle);
		quat(cvec4& v);
		quat(cmat4& m);
		mat4 mat() const;
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

	vec3 operator*(cvec3& v, cquat& q);
	vec4 operator*(cvec4& v, cquat& q);
	quat operator*(cfloat scalar, cquat& q);
	mat4 inverse(cmat4& m);
	quat inverse(cquat& q);
	quat conjugate(cquat& q);
	mat4 transpose(cmat4& m);
	mat4 translate(cmat4& m, cvec3& v);
	mat4 scale(cmat4& m, cvec3& v);
	mat4 rotate(cmat4& m, cfloat angle, cvec3& axis);
	mat4 perspective(cfloat fovy, cfloat aspect, cfloat zNear, cfloat zFar);
	mat4 ortho(cfloat left, cfloat right, cfloat bottom, cfloat top, cfloat zNear, cfloat zFar);
	mat4 lookAt(cvec3& eye, cvec3& center, cvec3& up);
	quat lookAt(cvec3& direction, cvec3& up);
	float length(cvec2& v);
	float length(cvec3& v);
	float length(cvec4& v);
	float length(cquat& q);
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
	vec3 reflect(cvec3& v, cvec3& normal);
	float mix(cfloat f1, cfloat f2, cfloat a);
	quat mix(cquat& q1, cquat& q2, cfloat a);
	quat lerp(cquat& q1, cquat& q2, cfloat a);
	quat slerp(cquat& q1, cquat& q2, cfloat a);
	float clamp(cfloat x, cfloat min, cfloat max);
	float minimum(cfloat a, cfloat b);
	float maximum(cfloat a, cfloat b);
}