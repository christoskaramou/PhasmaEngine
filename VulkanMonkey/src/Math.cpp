#include "../include/Math.h"
#include <assert.h>

namespace vm {
	vec2::vec2() : x(0.f), y(0.f)
	{ }

	vec2::vec2(cfloat value) : x(value), y(value)
	{ }

	vec2::vec2(cfloat x, cfloat y) : x(x), y(y)
	{ }

	vec2::vec2(cvec2 & v) : x(v.x), y(v.y)
	{ }

	vec2::vec2(cfloat * v) : x(v[0]), y(v[1])
	{ }

	vec2::vec2(cvec2 * v) : x(v->x), y(v->y)
	{ }

	void vec2::operator=(cvec2 & v)
	{
		x = v.x;
		y = v.y;
	}

	vec2 vec2::operator+(cvec2 & v) const
	{
		return vec2(x + v.x, y + v.y);
	}

	vec2 vec2::operator-() const
	{
		return vec2(-x, -y);
	}

	vec2 vec2::operator-(cvec2 & v) const
	{
		return vec2(x - v.x, y - v.y);
	}

	vec2 vec2::operator*(cvec2 & v) const
	{
		return vec2(x * v.x, y * v.y);
	}

	vec2 vec2::operator*(cfloat scalar) const
	{
		return vec2(x * scalar, y * scalar);
	}

	vec2 vec2::operator/(cvec2 & v) const
	{
		assert(v.x != 0.f && v.y != 0.f);
		return vec2(x / v.x, y / v.y);
	}

	vec2 vec2::operator/(cfloat scalar) const
	{
		assert(scalar != 0.f);
		return operator*(1.f/scalar);
	}

	void vec2::operator+=(cvec2 & v)
	{
		operator=(operator+(v));
	}

	void vec2::operator-=(cvec2 & v)
	{
		operator=(operator-(v));
	}

	void vec2::operator*=(cvec2 & v)
	{
		operator=(operator*(v));
	}

	void vec2::operator*=(cfloat scalar)
	{
		operator=(operator*(scalar));
	}

	void vec2::operator/=(cvec2 & v)
	{
		operator=(operator/(v));
	}

	void vec2::operator/=(cfloat scalar)
	{
		operator=(operator/(scalar));
	}

	bool vec2::operator==(cfloat * v)
	{
		return x == v[0] && y == v[1];
	}

	bool vec2::operator==(cvec2 * v)
	{
		return operator==((float*)v);
	}

	bool vec2::operator==(cvec2 & v)
	{
		return operator==((float*)&v);
	}

	float& vec2::operator[](unsigned i)
	{
		return (&x)[i];
	}

	float * vec2::ptr()
	{
		return &x;
	}

	vec3::vec3() : x(0.f), y(0.f), z(0.f)
	{ }

	vec3::vec3(cfloat value) : x(value), y(value), z(value)
	{ }

	vec3::vec3(cfloat x, cfloat y, cfloat z) : x(x), y(y), z(z)
	{ }

	vec3::vec3(cvec2 & v, cfloat z) : x(v.x), y(v.y), z(z)
	{ }

	vec3::vec3(cvec3 & v) : x(v.x), y(v.y), z(v.z)
	{ }

	vec3::vec3(cfloat * v) : x(v[0]), y(v[1]), z(v[2])
	{ }

	vec3::vec3(cvec3 * v) : x(v->x), y(v->y), z(v->z)
	{ }

	void vec3::operator=(cvec3 & v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
	}

	vec3 vec3::operator+(cvec3 & v) const
	{
		return vec3(x + v.x, y + v.y, z + v.z);
	}

	vec3 vec3::operator-() const
	{
		return vec3(-x, -y, -z);
	}

	vec3 vec3::operator-(cvec3 & v) const
	{
		return vec3(x - v.x, y - v.y, z - v.z);
	}

	vec3 vec3::operator*(cvec3 & v) const
	{
		return vec3(x * v.x, y * v.y, z * v.z);
	}

	vec3 vec3::operator*(cfloat scalar) const
	{
		return vec3(x * scalar, y * scalar, z * scalar);
	}

	vec3 vec3::operator/(cvec3 & v) const
	{
		assert(v.x != 0.f && v.y != 0.f && v.z != 0.f);
		return vec3(x / v.x, y / v.y,z / v.z);
	}

	vec3 vec3::operator/(cfloat scalar) const
	{
		assert(scalar != 0.f);
		return operator*(1.f/scalar);
	}

	void vec3::operator+=(cvec3 & v)
	{
		operator=(operator+(v));
	}

	void vec3::operator-=(cvec3 & v)
	{
		operator=(operator-(v));
	}

	void vec3::operator*=(cvec3 & v)
	{
		operator=(operator*(v));
	}

	void vec3::operator*=(cfloat scalar)
	{
		operator=(operator*(scalar));
	}

	void vec3::operator/=(cvec3 & v)
	{
		operator=(operator/(v));
	}

	void vec3::operator/=(cfloat scalar)
	{
		operator=(operator/(scalar));
	}

	bool vec3::operator==(cfloat * v)
	{
		return x == v[0] && y == v[1] && z == v[2];
	}

	bool vec3::operator==(cvec3 * v)
	{
		return operator==((float*)v);
	}

	bool vec3::operator==(cvec3 & v)
	{
		return operator==((float*)&v);
	}

	float & vec3::operator[](unsigned i)
	{
		return (&x)[i];
	}

	float * vec3::ptr()
	{
		return &x;
	}

	vec4::vec4() : x(0.f), y(0.f), z(0.f), w(0.f)
	{ }

	vec4::vec4(cfloat value) : x(value), y(value), z(value), w(value)
	{ }

	vec4::vec4(cfloat x, cfloat y, cfloat z, cfloat w) : x(x), y(y), z(z), w(w)
	{ }

	vec4::vec4(cvec3 & v, cfloat w) : x(v.x), y(v.y), z(v.z), w(w)
	{ }

	vec4::vec4(cvec4 & v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
		w = v.w;
	}

	vec4::vec4(cfloat * v)
	{
		x = v[0];
		y = v[1];
		z = v[2];
		w = v[3];
	}

	vec4::vec4(cvec4 * v)
	{
		x = v->x;
		y = v->y;
		z = v->z;
		w = v->w;
	}

	void vec4::operator=(cvec4 & v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
		w = v.w;
	}

	vec4 vec4::operator+(cvec4 & v) const
	{
		return vec4(x + v.x, y + v.y, z + v.z, w + v.w);
	}

	vec4 vec4::operator-() const
	{
		return vec4(-x, -x, -z, -w);
	}

	vec4 vec4::operator-(cvec4 & v) const
	{
		return vec4(x - v.x, y - v.y, z - v.z, w - v.w);
	}

	vec4 vec4::operator*(cvec4 & v) const
	{
		return vec4(x * v.x, y * v.y,z * v.z, w * v.w);
	}

	vec4 vec4::operator*(cfloat scalar) const
	{
		return vec4(x * scalar,y * scalar, z * scalar, w * scalar);
	}

	vec4 vec4::operator/(cvec4 & v) const
	{
		assert(v.x != 0.f && v.y != 0.f && v.z != 0.f && v.w != 0.f);
		return vec4(x / v.x, y / v.y, z / v.z, w / v.w);
	}

	vec4 vec4::operator/(cfloat scalar) const
	{
		assert(scalar != 0.f);
		return operator*(1.f / scalar);
	}

	void vec4::operator+=(cvec4 & v)
	{
		operator=(operator+(v));
	}

	void vec4::operator-=(cvec4 & v)
	{
		operator=(operator-(v));
	}

	void vec4::operator*=(cvec4 & v)
	{
		operator=(operator*(v));
	}

	void vec4::operator*=(cfloat scalar)
	{
		operator=(operator*(scalar));
	}

	void vec4::operator/=(cvec4 & v)
	{
		operator=(operator/(v));
	}

	void vec4::operator/=(cfloat scalar)
	{
		operator=(operator/(scalar));
	}

	bool vec4::operator==(cfloat * v)
	{
		return x == v[0] && y == v[1] && z == v[2] && w == v[3];
	}

	bool vec4::operator==(cvec4 * v)
	{
		return operator==((float*)v);
	}

	bool vec4::operator==(cvec4 & v)
	{
		return operator==((float*)&v);
	}

	float & vec4::operator[](unsigned i)
	{
		return (&x)[i];
	}

	float * vec4::ptr()
	{
		return &x;
	}

	mat4::mat4() : _v{
		col(0.f, 0.f, 0.f, 0.f),
		col(0.f, 0.f, 0.f, 0.f),
		col(0.f, 0.f, 0.f, 0.f),
		col(0.f, 0.f, 0.f, 0.f)
	}
	{ }

	mat4::mat4(cfloat diagonal) : _v{
		col(diagonal, 0.f, 0.f, 0.f),
		col(0.f, diagonal, 0.f, 0.f),
		col(0.f, 0.f, diagonal, 0.f),
		col(0.f, 0.f, 0.f, diagonal)
	}
	{ }

	mat4::mat4(cfloat * m) : _v{
		col(m[0], m[1], m[2], m[3]),
		col(m[4], m[5], m[6], m[7]),
		col(m[8], m[9], m[10], m[11]),
		col(m[12], m[13], m[14], m[15])
	}
	{ }

	mat4::mat4(cmat4 & m) : _v{
		m._v[0],
		m._v[1],
		m._v[2],
		m._v[3]
	}
	{ }

	mat4::mat4(cmat4 * m) : _v{
		m->_v[0],
		m->_v[1],
		m->_v[2],
		m->_v[3]
	}
	{ }

	mat4::mat4(ccol & v0, ccol & v1, ccol & v2, ccol & v3) : _v{
		v0,
		v1,
		v2,
		v3
	}
	{ }

	mat4::mat4
	(
		cfloat& x0, cfloat& y0, cfloat& z0, cfloat& w0,
		cfloat& x1, cfloat& y1, cfloat& z1, cfloat& w1,
		cfloat& x2, cfloat& y2, cfloat& z2, cfloat& w2,
		cfloat& x3, cfloat& y3, cfloat& z3, cfloat& w3
	) : _v{
		col(x0, y0, z0, w0),
		col(x1, y1, z1, w1),
		col(x2, y2, z2, w2),
		col(x3, y3, z3, w3)
	}
	{ }

	cmat4& mat4::identity()
	{
		static cmat4 idendity = mat4(1.f);
		return idendity;
	}

	void mat4::operator=(cmat4 & m)
	{
		_v[0] = m._v[0];
		_v[1] = m._v[1];
		_v[2] = m._v[2];
		_v[3] = m._v[3];
	}

	mat4 mat4::operator*(cmat4 & m) const
	{
		return {
			_v[0] * m._v[0].x + _v[1] * m._v[0].y + _v[2] * m._v[0].z + _v[3] * m._v[0].w,
			_v[0] * m._v[1].x + _v[1] * m._v[1].y + _v[2] * m._v[1].z + _v[3] * m._v[1].w,
			_v[0] * m._v[2].x + _v[1] * m._v[2].y + _v[2] * m._v[2].z + _v[3] * m._v[2].w,
			_v[0] * m._v[3].x + _v[1] * m._v[3].y + _v[2] * m._v[3].z + _v[3] * m._v[3].w
		};	
	}

	vec4 mat4::operator*(cvec4 & v) const
	{
		return
			_v[0] * vec4(v.x) +
			_v[1] * vec4(v.y) +
			_v[2] * vec4(v.z) +
			_v[3] * vec4(v.w);
	}

	mat4 mat4::operator*(cfloat scalar)
	{
		return mat4(
			_v[0] * scalar,
			_v[1] * scalar,
			_v[2] * scalar,
			_v[3] * scalar
		);
	}

	bool mat4::operator==(cfloat * m)
	{
		return 
			_v[0].x == m[0] && _v[0].y == m[1] && _v[0].z == m[2] && _v[0].w == m[3] &&
			_v[1].x == m[4] && _v[1].y == m[5] && _v[1].z == m[6] && _v[1].w == m[7] &&
			_v[2].x == m[8] && _v[2].y == m[9] && _v[2].z == m[10] && _v[2].w == m[11] &&
			_v[3].x == m[12] && _v[3].y == m[13] && _v[3].z == m[14] && _v[3].w == m[15];
	}

	bool mat4::operator==(cmat4 * m)
	{
		return operator==((float*)m);
	}

	bool mat4::operator==(cmat4 & m)
	{
		return operator==((float*)&m);
	}

	vec4 & mat4::operator[](unsigned i)
	{
		return _v[i];
	}

	float * mat4::ptr()
	{
		return &_v[0].x;
	}

	mat4 inverse(cmat4 & m)
	{
		cfloat Coef00 = m._v[2].z * m._v[3].w - m._v[3].z * m._v[2].w;
		cfloat Coef02 = m._v[1].z * m._v[3].w - m._v[3].z * m._v[1].w;
		cfloat Coef03 = m._v[1].z * m._v[2].w - m._v[2].z * m._v[1].w;

		cfloat Coef04 = m._v[2].y * m._v[3].w - m._v[3].y * m._v[2].w;
		cfloat Coef06 = m._v[1].y * m._v[3].w - m._v[3].y * m._v[1].w;
		cfloat Coef07 = m._v[1].y * m._v[2].w - m._v[2].y * m._v[1].w;

		cfloat Coef08 = m._v[2].y * m._v[3].z - m._v[3].y * m._v[2].z;
		cfloat Coef10 = m._v[1].y * m._v[3].z - m._v[3].y * m._v[1].z;
		cfloat Coef11 = m._v[1].y * m._v[2].z - m._v[2].y * m._v[1].z;

		cfloat Coef12 = m._v[2].x * m._v[3].w - m._v[3].x * m._v[2].w;
		cfloat Coef14 = m._v[1].x * m._v[3].w - m._v[3].x * m._v[1].w;
		cfloat Coef15 = m._v[1].x * m._v[2].w - m._v[2].x * m._v[1].w;

		cfloat Coef16 = m._v[2].x * m._v[3].z - m._v[3].x * m._v[2].z;
		cfloat Coef18 = m._v[1].x * m._v[3].z - m._v[3].x * m._v[1].z;
		cfloat Coef19 = m._v[1].x * m._v[2].z - m._v[2].x * m._v[1].z;

		cfloat Coef20 = m._v[2].x * m._v[3].y - m._v[3].x * m._v[2].y;
		cfloat Coef22 = m._v[1].x * m._v[3].y - m._v[3].x * m._v[1].y;
		cfloat Coef23 = m._v[1].x * m._v[2].y - m._v[2].x * m._v[1].y;

		cvec4 Fac0(Coef00, Coef00, Coef02, Coef03);
		cvec4 Fac1(Coef04, Coef04, Coef06, Coef07);
		cvec4 Fac2(Coef08, Coef08, Coef10, Coef11);
		cvec4 Fac3(Coef12, Coef12, Coef14, Coef15);
		cvec4 Fac4(Coef16, Coef16, Coef18, Coef19);
		cvec4 Fac5(Coef20, Coef20, Coef22, Coef23);

		cvec4 Vec0(m._v[1].x, m._v[0].x, m._v[0].x, m._v[0].x);
		cvec4 Vec1(m._v[1].y, m._v[0].y, m._v[0].y, m._v[0].y);
		cvec4 Vec2(m._v[1].z, m._v[0].z, m._v[0].z, m._v[0].z);
		cvec4 Vec3(m._v[1].w, m._v[0].w, m._v[0].w, m._v[0].w);

		cvec4 Inv0(Vec1 * Fac0 - Vec2 * Fac1 + Vec3 * Fac2);
		cvec4 Inv1(Vec0 * Fac0 - Vec2 * Fac3 + Vec3 * Fac4);
		cvec4 Inv2(Vec0 * Fac1 - Vec1 * Fac3 + Vec3 * Fac5);
		cvec4 Inv3(Vec0 * Fac2 - Vec1 * Fac4 + Vec2 * Fac5);

		cvec4 SignA(+1, -1, +1, -1);
		cvec4 SignB(-1, +1, -1, +1);
		mat4 Inverse(Inv0 * SignA, Inv1 * SignB, Inv2 * SignA, Inv3 * SignB);

		cvec4 Row0(Inverse[0][0], Inverse[1][0], Inverse[2][0], Inverse[3][0]);

		cvec4 Dot0(m._v[0] * Row0);
		cfloat Dot1 = (Dot0.x + Dot0.y) + (Dot0.z + Dot0.w);

		cfloat OneOverDeterminant = 1.f / Dot1;

		return Inverse * OneOverDeterminant;
	}

	mat4 transpose(cmat4& m)
	{
		return mat4(
			m._v[0].x, m._v[1].x, m._v[2].x, m._v[3].x,
			m._v[0].y, m._v[1].y, m._v[2].y, m._v[3].y,
			m._v[0].z, m._v[1].z, m._v[2].z, m._v[3].z,
			m._v[0].w, m._v[1].w, m._v[2].w, m._v[3].w
		);
	}

	mat4 translate(cmat4& m, cvec3& v)
	{
		return mat4(
			m._v[0],
			m._v[1],
			m._v[2],
			m._v[0] * v.x + m._v[1] * v.y + m._v[2] * v.z + m._v[3]
		);
	}

	mat4 scale(cmat4 & m, cvec3 & v)
	{
		return mat4(
			m._v[0] * v.x,
			m._v[1] * v.y,
			m._v[2] * v.z,
			m._v[3]
		);
	}

	mat4 rotate(cmat4 & m, cfloat angle, cvec3 & v)
	{
		cfloat c = cos(angle);
		cfloat s = sin(angle);

		cvec3 axis(normalize(v));
		cvec3 temp(axis * (1.f - c));

		cfloat r00 = c + temp.x * axis.x;
		cfloat r01 = temp.x * axis.y + s * axis.z;
		cfloat r02 = temp.x * axis.z - s * axis.y;

		cfloat r10 = temp.y * axis.x - s * axis.z;
		cfloat r11 = c + temp.y * axis.y;
		cfloat r12 = temp.y * axis.z + s * axis.x;

		cfloat r20 = temp.z * axis.x + s * axis.y;
		cfloat r21 = temp.z * axis.y - s * axis.x;
		cfloat r22 = c + temp.z * axis.z;

		return mat4(
			m._v[0] * r00 + m._v[1] * r01 + m._v[2] * r02,
			m._v[0] * r10 + m._v[1] * r11 + m._v[2] * r12,
			m._v[0] * r20 + m._v[1] * r21 + m._v[2] * r22,
			m._v[3]
		);
	}

	mat4 perspective(cfloat fovy, cfloat aspect, cfloat zNear, cfloat zFar)
	{
		cfloat tanHalfFovy = tan(fovy / 2.f);
		cfloat m00 = 1.f / (aspect * tanHalfFovy);
		cfloat m11 = 1.f / (tanHalfFovy);
		cfloat m22 = zFar / (zFar - zNear);
		cfloat m23 = 1.f;
		cfloat m32 = -(zFar * zNear) / (zFar - zNear);

		return mat4(
			m00, 0.f, 0.f, 0.f,
			0.f, m11, 0.f, 0.f,
			0.f, 0.f, m22, m23,
			0.f, 0.f, m32, 0.f
		);
	}

	mat4 ortho(cfloat left, cfloat right, cfloat bottom, cfloat top, cfloat zNear, cfloat zFar)
	{
		cfloat m00 = 2.f / (right - left);
		cfloat m11 = 2.f / (top - bottom);
		cfloat m22 = 1.f / (zFar - zNear);
		cfloat m30 = -(right + left) / (right - left);
		cfloat m31 = -(top + bottom) / (top - bottom);
		cfloat m32 = -zNear / (zFar - zNear);

		return mat4(
			m00, 0.f, 0.f, 0.f,
			0.f, m11, 0.f, 0.f,
			0.f, 0.f, m22, 0.f,
			m30, m31, m32, 1.f
		);
	}

	mat4 lookAt(cvec3& eye, cvec3& center, cvec3& up)
	{
		cvec3 f(normalize(center - eye));
		cvec3 r(normalize(cross(up, f)));
		cvec3 u(cross(f, r));

		cfloat m30 = -dot(r, eye);
		cfloat m31 = -dot(u, eye);
		cfloat m32 = -dot(f, eye);
		return mat4(
			r.x, u.x, f.x, 0.f,
			r.y, u.y, f.y, 0.f,
			r.z, u.z, f.z, 0.f,
			m30, m31, m32, 1.f
		);
	}

	float length(cvec2 & v)
	{
		return sqrt(dot(v, v));
	}

	float length(cvec3 & v)
	{
		return sqrt(dot(v, v));
	}

	float length(cvec4 & v)
	{
		return sqrt(dot(v, v));
	}

	vec2 normalize(cvec2 & v)
	{
		return v * inversesqrt(dot(v, v));
	}

	vec3 normalize(cvec3 & v)
	{
		return v * inversesqrt(dot(v, v));
	}

	vec4 normalize(cvec4 & v)
	{
		return v * inversesqrt(dot(v, v));
	}

	float dot(cvec2 & v1, cvec2 & v2)
	{
		cvec2 vec(v1 * v2);
		return vec.x + vec.y;
	}

	float dot(cvec3 & v1, cvec3 & v2)
	{
		cvec3 vec(v1 * v2);
		return vec.x + vec.y + vec.z;
	}

	float dot(cvec4 & v1, cvec4 & v2)
	{
		cvec4 vec(v1 * v2);
		return vec.x + vec.y + vec.z + vec.w;
	}

	vec3 cross(cvec3 & v1, cvec3 & v2)
	{
		return vec3(
			v1.y * v2.z - v2.y * v1.z,
			v1.z * v2.x - v2.z * v1.x,
			v1.x * v2.y - v2.x * v1.y);
	}

	float inversesqrt(cfloat x)
	{
		return 1.f / sqrt(x);
	}

	float radians(cfloat degrees)
	{
		 return degrees * 0.01745329251994329576923690768489f;
	}

	float degrees(cfloat radians)
	{
		return radians * 57.295779513082320876798154814105f;
	}

}
