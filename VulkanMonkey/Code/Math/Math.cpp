#include "Math.h"
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
		return vec2(x / v.x, y / v.y);
	}

	vec2 vec2::operator/(cfloat scalar) const
	{
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

	bool vec2::operator==(cfloat * v) const
	{
		return x == v[0] && y == v[1];
	}

	bool vec2::operator==(cvec2 * v) const
	{
		return operator==(&v->x);
	}

	bool vec2::operator==(cvec2 & v) const
	{
		return operator==(&v.x);
	}

	bool vec2::operator!=(cfloat * v) const
	{
		return !operator==(v);
	}

	bool vec2::operator!=(cvec2 * v) const
	{
		return !operator==(v);
	}

	bool vec2::operator!=(cvec2 & v) const
	{
		return !operator==(v);
	}

	float& vec2::operator[](unsigned i)
	{
		assert(i < 2);
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

	vec3::vec3(cvec4 & v) : x(v.x), y(v.y), z(v.z)
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
		return vec3(x / v.x, y / v.y,z / v.z);
	}

	vec3 vec3::operator/(cfloat scalar) const
	{
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

	bool vec3::operator==(cfloat * v) const
	{
		return x == v[0] && y == v[1] && z == v[2];
	}

	bool vec3::operator==(cvec3 * v) const
	{
		return operator==(&v->x);
	}

	bool vec3::operator==(cvec3 & v) const
	{
		return operator==(&v.x);
	}

	bool vec3::operator!=(cfloat * v) const
	{
		return !operator==(v);
	}

	bool vec3::operator!=(cvec3 * v) const
	{
		return !operator==(v);
	}

	bool vec3::operator!=(cvec3 & v) const
	{
		return !operator==(v);
	}

	float & vec3::operator[](unsigned i)
	{
		assert(i < 3);
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
		return vec4(x * scalar, y * scalar, z * scalar, w * scalar);
	}

	vec4 vec4::operator/(cvec4 & v) const
	{
		return vec4(x / v.x, y / v.y, z / v.z, w / v.w);
	}

	vec4 vec4::operator/(cfloat scalar) const
	{
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

	bool vec4::operator==(cfloat * v) const
	{
		return x == v[0] && y == v[1] && z == v[2] && w == v[3];
	}

	bool vec4::operator==(cvec4 * v) const
	{
		return operator==(&v->x);
	}

	bool vec4::operator==(cvec4 & v) const
	{
		return operator==(&v.x);
	}

	bool vec4::operator!=(cfloat * v) const
	{
		return !operator==(v);
	}

	bool vec4::operator!=(cvec4 * v) const
	{
		return !operator==(v);
	}

	bool vec4::operator!=(cvec4 & v) const
	{
		return !operator==(v);
	}

	float & vec4::operator[](unsigned i)
	{
		assert(i < 4);
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

	mat4::mat4(cquat & q)
	{
		cfloat qxx(q.x * q.x);
		cfloat qyy(q.y * q.y);
		cfloat qzz(q.z * q.z);
		cfloat qxz(q.x * q.z);
		cfloat qxy(q.x * q.y);
		cfloat qyz(q.y * q.z);
		cfloat qwx(q.w * q.x);
		cfloat qwy(q.w * q.y);
		cfloat qwz(q.w * q.z);

		cfloat r00 = 1.f - 2.f * (qyy + qzz);
		cfloat r01 = 2.f * (qxy + qwz);
		cfloat r02 = 2.f * (qxz - qwy);

		cfloat r10 = 2.f * (qxy - qwz);
		cfloat r11 = 1.f - 2.f * (qxx + qzz);
		cfloat r12 = 2.f * (qyz + qwx);

		cfloat r20 = 2.f * (qxz + qwy);
		cfloat r21 = 2.f * (qyz - qwx);
		cfloat r22 = 1.f - 2.f * (qxx + qyy);

		_v[0] = col(r00, r01, r02, 0.f);
		_v[1] = col(r10, r11, r12, 0.f);
		_v[2] = col(r20, r21, r22, 0.f);
		_v[3] = col(0.f, 0.f, 0.f, 1.f);
	}

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

	cmat4 mat4::identity()
	{
		return mat4(1.f);
	}

	quat mat4::quaternion() const
	{
		return quat(eulerAngles());
	}

	vec3 mat4::eulerAngles() const
	{
		return rotation().eulerAngles();
	}

	float mat4::pitch() const
	{
		return rotation().pitch();
	}

	float mat4::yaw() const
	{
		return rotation().yaw();
	}

	float mat4::roll() const
	{
		return rotation().roll();
	}

	vec3 mat4::translation()
	{
		return vec3(_v[3]);
	}

	vec3 mat4::scale() const
	{
		cfloat xSign = _v[0].x * _v[0].y * _v[0].z * _v[0].w < 0.f ? -1.f : 1.f;
		cfloat ySign = _v[1].x * _v[1].y * _v[1].z * _v[1].w < 0.f ? -1.f : 1.f;
		cfloat zSign = _v[2].x * _v[2].y * _v[2].z * _v[2].w < 0.f ? -1.f : 1.f;

		return vec3(
			xSign * length(vec3(_v[0])),
			ySign * length(vec3(_v[1])),
			zSign * length(vec3(_v[2]))
		);
	}

	quat mat4::rotation() const
	{
		cvec3 s(scale());
		if (abs(s.x * s.y * s.z) < FLT_EPSILON)
			return quat::identity();

		return quat(mat4(
			_v[0].x / s.x, _v[0].y / s.x, _v[0].z / s.x, 0.f,
			_v[1].x / s.y, _v[1].y / s.y, _v[1].z / s.y, 0.f,
			_v[2].x / s.z, _v[2].y / s.z, _v[2].z / s.z, 0.f,
			0.f, 0.f, 0.f, 1.f
		)
		);
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
		return mat4(
			_v[0] * m._v[0].x + _v[1] * m._v[0].y + _v[2] * m._v[0].z + _v[3] * m._v[0].w,
			_v[0] * m._v[1].x + _v[1] * m._v[1].y + _v[2] * m._v[1].z + _v[3] * m._v[1].w,
			_v[0] * m._v[2].x + _v[1] * m._v[2].y + _v[2] * m._v[2].z + _v[3] * m._v[2].w,
			_v[0] * m._v[3].x + _v[1] * m._v[3].y + _v[2] * m._v[3].z + _v[3] * m._v[3].w
		);	
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

	bool mat4::operator==(cfloat * m) const
	{
		return 
			_v[0].x == m[0] && _v[0].y == m[1] && _v[0].z == m[2] && _v[0].w == m[3] &&
			_v[1].x == m[4] && _v[1].y == m[5] && _v[1].z == m[6] && _v[1].w == m[7] &&
			_v[2].x == m[8] && _v[2].y == m[9] && _v[2].z == m[10] && _v[2].w == m[11] &&
			_v[3].x == m[12] && _v[3].y == m[13] && _v[3].z == m[14] && _v[3].w == m[15];
	}

	bool mat4::operator==(cmat4 * m) const
	{
		return operator==(&m->_v[0].x);
	}

	bool mat4::operator==(cmat4 & m) const
	{
		return operator==(&m._v[0].x);
	}

	bool mat4::operator!=(cfloat * m) const
	{
		return !operator==(m);
	}

	bool mat4::operator!=(cmat4 * m) const
	{
		return !operator==(m);
	}

	bool mat4::operator!=(cmat4 & m) const
	{
		return !operator==(m);
	}

	vec4 & mat4::operator[](unsigned i)
	{
		assert(i < 4);
		return _v[i];
	}

	float * mat4::ptr()
	{
		return &_v[0].x;
	}

	quat::quat() : x(0.f), y(0.f), z(0.f), w(1.f)
	{ }

	quat::quat(cfloat * q) : x(q[0]), y(q[1]), z(q[2]), w(q[3])
	{ }

	quat::quat(cquat * q) : x(q->x), y(q->y), z(q->z), w(q->w)
	{ }

	quat::quat(cquat & q) : x(q.x), y(q.y), z(q.z), w(q.w)
	{ }

	quat::quat(cfloat f, cvec3& v) : x(v.x), y(v.y), z(v.z), w(f)
	{ }

	quat::quat(cfloat w, cfloat x, cfloat y, cfloat z) : x(x), y(y), z(z), w(w)
	{ }

	quat::quat(cvec3 & u, cvec3 & v)
	{
		float norm_u_norm_v = sqrt(dot(u, u) * dot(v, v));
		float real_part = norm_u_norm_v + dot(u, v);
		vec3 t;

		if (real_part < 1.e-6f * norm_u_norm_v) {
			real_part = 0.f;
			t = abs(u.x) > abs(u.z) ? vec3(-u.y, u.x, 0.f) : vec3(0.f, -u.z, u.y);
		}
		else {
			t = cross(u, v);
		}

		*this = normalize(quat(real_part, t.x, t.y, t.z));
	}

	quat::quat(cvec3 & eulerAngle)
	{
		cvec3 c(cos(eulerAngle.x * .5f), cos(eulerAngle.y * .5f), cos(eulerAngle.z * .5f));
		cvec3 s(sin(eulerAngle.x * .5f), sin(eulerAngle.y * .5f), sin(eulerAngle.z * .5f));

		w = c.x * c.y * c.z + s.x * s.y * s.z;
		x = s.x * c.y * c.z - c.x * s.y * s.z;
		y = c.x * s.y * c.z + s.x * c.y * s.z;
		z = c.x * c.y * s.z - s.x * s.y * c.z;
	}

	quat::quat(cvec4 & eulerAngle)
	{
		// w of vec4 is ignored
		cvec3 c(cos(eulerAngle.x * .5f), cos(eulerAngle.y * .5f), cos(eulerAngle.z * .5f));
		cvec3 s(sin(eulerAngle.x * .5f), sin(eulerAngle.y * .5f), sin(eulerAngle.z * .5f));

		w = c.x * c.y * c.z + s.x * s.y * s.z;
		x = s.x * c.y * c.z - c.x * s.y * s.z;
		y = c.x * s.y * c.z + s.x * c.y * s.z;
		z = c.x * c.y * s.z - s.x * s.y * c.z;
	}

	quat::quat(cmat4 & m)
	{
		float fourXSquaredMinus1 = m._v[0].x - m._v[1].y - m._v[2].z;
		float fourYSquaredMinus1 = m._v[1].y - m._v[0].x - m._v[2].z;
		float fourZSquaredMinus1 = m._v[2].z - m._v[0].x - m._v[1].y;
		float fourWSquaredMinus1 = m._v[0].x + m._v[1].y + m._v[2].z;

		int biggestIndex = 0;
		float fourBiggestSquaredMinus1 = fourWSquaredMinus1;
		if (fourXSquaredMinus1 > fourBiggestSquaredMinus1)
		{
			fourBiggestSquaredMinus1 = fourXSquaredMinus1;
			biggestIndex = 1;
		}
		if (fourYSquaredMinus1 > fourBiggestSquaredMinus1)
		{
			fourBiggestSquaredMinus1 = fourYSquaredMinus1;
			biggestIndex = 2;
		}
		if (fourZSquaredMinus1 > fourBiggestSquaredMinus1)
		{
			fourBiggestSquaredMinus1 = fourZSquaredMinus1;
			biggestIndex = 3;
		}

		float biggestVal = sqrt(fourBiggestSquaredMinus1 + 1.f) * .5f;
		float mult = .25f / biggestVal;

		switch (biggestIndex)
		{
		case 0:
			w = biggestVal;
			x = (m._v[1].z - m._v[2].y) * mult;
			y = (m._v[2].x - m._v[0].z) * mult;
			z = (m._v[0].y - m._v[1].x) * mult;
			break;
		case 1:
			w = (m._v[1].z - m._v[2].y) * mult;
			x = biggestVal;
			y = (m._v[0].y + m._v[1].x) * mult;
			z = (m._v[2].x + m._v[0].z) * mult;
			break;
		case 2:
			w = (m._v[2].x - m._v[0].z) * mult;
			x = (m._v[0].y + m._v[1].x) * mult;
			y = biggestVal;
			z = (m._v[1].z + m._v[2].y) * mult;
			break;
		case 3:
			w = (m._v[0].y - m._v[1].x) * mult;
			x = (m._v[2].x + m._v[0].z) * mult;
			y = (m._v[1].z + m._v[2].y) * mult;
			z = biggestVal;
			break;
		default: // Silence a -Wswitch-default warning in GCC. Should never actually get here. Assert is just for sanity.
			assert(false);
			w = 1.f;
			x = 0.f;
			y = 0.f;
			z = 0.f;
		}
	}

	cquat quat::identity()
	{
		return quat(1.f, 0.f, 0.f, 0.f);
	}

	mat4 quat::matrix() const
	{
		return mat4(*this);
	}

	vec3 quat::eulerAngles() const
	{
		return vec3(pitch(), yaw(), roll());
	}

	float quat::pitch() const
	{
		cfloat _x = w * w - x * x - y * y + z * z;
		cfloat _y = 2.f * (y * z + w * x);
		
		return abs(_x) < FLT_EPSILON && abs(_y) < FLT_EPSILON ? 2.f * atan2(x, w) : atan2(_y, _x);
	}

	float quat::yaw() const
	{
		return asin(clamp(-2.f * (x * z - w * y), -1.f, 1.f));
	}

	float quat::roll() const
	{
		return atan2(2.f * (x * y + w * z), w * w + x * x - y * y - z * z);
	}

	void quat::operator=(cquat & q)
	{
		w = q.w;
		x = q.x;
		y = q.y;
		z = q.z;
	}

	quat quat::operator+(cquat & q) const
	{
		return quat(w + q.w, x + q.x, y + q.y, z + q.z);
	}

	quat quat::operator-(cquat & q) const
	{
		return quat(w - q.w, x - q.x, y - q.y, z - q.z);
	}

	quat quat::operator-() const
	{
		return quat(-w, -x, -y, -z);
	}

	quat quat::operator*(cfloat scalar) const
	{
		return quat(w * scalar, x * scalar, y * scalar, z * scalar);
	}

	vec3 quat::operator*(cvec3 & v) const
	{
		cvec3 qv(x, y, z);
		cvec3 uv(cross(qv, v));
		cvec3 uuv(cross(qv, uv));

		return v + ((uv * w) + uuv) * 2.f;
	}

	vec4 quat::operator*(cvec4 & v) const
	{
		return vec4(*this * vec3(v), v.w);
	}

	quat quat::operator*(cquat & q) const
	{
		return quat(
			w * q.w - x * q.x - y * q.y - z * q.z,
			w * q.x + x * q.w + y * q.z - z * q.y,
			w * q.y + y * q.w + z * q.x - x * q.z,
			w * q.z + z * q.w + x * q.y - y * q.x
		);
	}

	quat quat::operator/(cfloat scalar) const
	{
		return operator*(1.f / scalar);
	}

	bool quat::operator==(cquat & q) const
	{
		return x == q.x && y == q.y && z == q.z && w == q.w;
	}

	bool quat::operator!=(cquat & q) const
	{
		return !quat::operator==(q);
	}

	float & quat::operator[](unsigned i)
	{
		assert(i < 4);
		return (&x)[i];
	}

	float * quat::ptr()
	{
		return &x;
	}

	Ray::Ray(cvec3 o, cvec3 d) : o(o), d(normalize(d))
	{ }

	vec2 operator*(cfloat scalar, cvec2 & v)
	{
		return v * scalar;
	}

	vec3 operator*(cfloat scalar, cvec3 & v)
	{
		return v * scalar;
	}

	vec4 operator*(cfloat scalar, cvec4 & v)
	{
		return v * scalar;
	}

	vec3 operator*(cvec3 & v, cquat & q)
	{
		return inverse(q) * v;
	}

	vec4 operator*(cvec4 & v, cquat & q)
	{
		return inverse(q) * v;
	}

	quat operator*(cfloat scalar, cquat & q)
	{
		return q * scalar;
	}

	mat4 inverse(cmat4 & m)
	{
		cfloat c00 = m._v[2].z * m._v[3].w - m._v[3].z * m._v[2].w;
		cfloat c02 = m._v[1].z * m._v[3].w - m._v[3].z * m._v[1].w;
		cfloat c03 = m._v[1].z * m._v[2].w - m._v[2].z * m._v[1].w;

		cfloat c04 = m._v[2].y * m._v[3].w - m._v[3].y * m._v[2].w;
		cfloat c06 = m._v[1].y * m._v[3].w - m._v[3].y * m._v[1].w;
		cfloat c07 = m._v[1].y * m._v[2].w - m._v[2].y * m._v[1].w;

		cfloat c08 = m._v[2].y * m._v[3].z - m._v[3].y * m._v[2].z;
		cfloat c10 = m._v[1].y * m._v[3].z - m._v[3].y * m._v[1].z;
		cfloat c11 = m._v[1].y * m._v[2].z - m._v[2].y * m._v[1].z;

		cfloat c12 = m._v[2].x * m._v[3].w - m._v[3].x * m._v[2].w;
		cfloat c14 = m._v[1].x * m._v[3].w - m._v[3].x * m._v[1].w;
		cfloat c15 = m._v[1].x * m._v[2].w - m._v[2].x * m._v[1].w;

		cfloat c16 = m._v[2].x * m._v[3].z - m._v[3].x * m._v[2].z;
		cfloat c18 = m._v[1].x * m._v[3].z - m._v[3].x * m._v[1].z;
		cfloat c19 = m._v[1].x * m._v[2].z - m._v[2].x * m._v[1].z;

		cfloat c20 = m._v[2].x * m._v[3].y - m._v[3].x * m._v[2].y;
		cfloat c22 = m._v[1].x * m._v[3].y - m._v[3].x * m._v[1].y;
		cfloat c23 = m._v[1].x * m._v[2].y - m._v[2].x * m._v[1].y;

		cvec4 f0(c00, c00, c02, c03);
		cvec4 f1(c04, c04, c06, c07);
		cvec4 f2(c08, c08, c10, c11);
		cvec4 f3(c12, c12, c14, c15);
		cvec4 f4(c16, c16, c18, c19);
		cvec4 f5(c20, c20, c22, c23);

		cvec4 v0(m._v[1].x, m._v[0].x, m._v[0].x, m._v[0].x);
		cvec4 v1(m._v[1].y, m._v[0].y, m._v[0].y, m._v[0].y);
		cvec4 v2(m._v[1].z, m._v[0].z, m._v[0].z, m._v[0].z);
		cvec4 v3(m._v[1].w, m._v[0].w, m._v[0].w, m._v[0].w);

		cvec4 i0(v1 * f0 - v2 * f1 + v3 * f2);
		cvec4 i1(v0 * f0 - v2 * f3 + v3 * f4);
		cvec4 i2(v0 * f1 - v1 * f3 + v3 * f5);
		cvec4 i3(v0 * f2 - v1 * f4 + v2 * f5);

		cvec4 sA(+1, -1, +1, -1);
		cvec4 sB(-1, +1, -1, +1);
		mat4 i(i0 * sA, i1 * sB, i2 * sA, i3 * sB);

		cvec4 r0(i[0][0], i[1][0], i[2][0], i[3][0]);

		cvec4 d0(m._v[0] * r0);
		cfloat d1 = (d0.x + d0.y) + (d0.z + d0.w);

		return i * (1.f / d1);
	}

	quat inverse(cquat & q)
	{
		return conjugate(q) / dot(q, q);
	}

	quat conjugate(cquat & q)
	{
		return quat(q.w, -q.x, -q.y, -q.z);
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

	mat4 rotate(cmat4 & m, cfloat angle, cvec3 & axis)
	{
		cfloat c = cos(angle);
		cfloat s = sin(angle);

		cvec3 naxis(normalize(axis));
		cvec3 temp(naxis * (1.f - c));

		cfloat r00 = c + temp.x * naxis.x;
		cfloat r01 = temp.x * naxis.y + s * naxis.z;
		cfloat r02 = temp.x * naxis.z - s * naxis.y;

		cfloat r10 = temp.y * naxis.x - s * naxis.z;
		cfloat r11 = c + temp.y * naxis.y;
		cfloat r12 = temp.y * naxis.z + s * naxis.x;

		cfloat r20 = temp.z * naxis.x + s * naxis.y;
		cfloat r21 = temp.z * naxis.y - s * naxis.x;
		cfloat r22 = c + temp.z * naxis.z;

		return mat4(
			m._v[0] * r00 + m._v[1] * r01 + m._v[2] * r02,
			m._v[0] * r10 + m._v[1] * r11 + m._v[2] * r12,
			m._v[0] * r20 + m._v[1] * r21 + m._v[2] * r22,
			m._v[3]
		);
	}

	quat rotate(cquat & q, cfloat angle, cvec3 & axis)
	{
		vec3 axisNorm = normalize(axis);

		cfloat c = cos(angle * .5f);
		cfloat s = sin(angle * .5f);

		return q * quat(c, axisNorm.x * s, axisNorm.y * s, axisNorm.z * s);
	}

	// rotation, scale, translation
	mat4 transform(cquat& r, cvec3 s, cvec3 t)
	{
		mat4 m = r.matrix();
		return mat4(
			m[0] * s.x,
			m[1] * s.y,
			m[2] * s.z,
			col(t, 1.0f)
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

	mat4 lookAt(cvec3& eye, cvec3& front, cvec3& right, cvec3& up)
	{
		cvec3 f(front);
		cvec3 r(right);
		cvec3 u(up);

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

	quat lookAt(cvec3& front, cvec3& right, cvec3& up)
	{
		cvec3 f(front);
		cvec3 r(right);
		cvec3 u(up);
		cvec4 fill(0.f, 0.f, 0.f, 1.f);

		return quat(
			mat4(
				row(r, 0.f),
				row(u, 0.f),
				row(f, 0.f),
				fill
			)
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

	float length(cquat & q)
	{
		return sqrt(dot(q, q));
	}

	float lengthSquared(cvec2 & v)
	{
		return dot(v, v);
	}

	float lengthSquared(cvec3 & v)
	{
		return dot(v, v);
	}

	float lengthSquared(cvec4 & v)
	{
		return dot(v, v);
	}

	float lengthSquared(cquat & q)
	{
		return dot(q, q);
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

	quat normalize(cquat & q)
	{
		cfloat len = length(q);
		if (len <= 0.f)
			return quat(1.f, 0.f, 0.f, 0.f);
		cfloat oneOverLen = 1.f / len;
		return quat(q.w * oneOverLen, q.x * oneOverLen, q.y * oneOverLen, q.z * oneOverLen);
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

	float dot(cquat & q1, cquat & q2)
	{
		cvec4 q(q1.x * q2.x, q1.y * q2.y, q1.z * q2.z, q1.w * q2.w);
		return q.x + q.y + q.z + q.w;
	}

	vec3 cross(cvec3 & v1, cvec3 & v2)
	{
		return vec3(
			v1.y * v2.z - v2.y * v1.z,
			v1.z * v2.x - v2.z * v1.x,
			v1.x * v2.y - v2.x * v1.y);
	}

	quat cross(cquat & q1, cquat & q2)
	{
		return quat(
			q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z,
			q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
			q1.w * q2.y + q1.y * q2.w + q1.z * q2.x - q1.x * q2.z,
			q1.w * q2.z + q1.z * q2.w + q1.x * q2.y - q1.y * q2.x
		);
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

	vec3 radians(cvec3 & v)
	{
		return v * 0.01745329251994329576923690768489f;
	}

	vec3 degrees(cvec3 & v)
	{
		return v * 57.295779513082320876798154814105f;
	}

	vec3 reflect(cvec3 & v, cvec3 & normal)
	{
		return v - normal * dot(normal, v) * 2.f;
	}

	float mix(cfloat f1, cfloat f2, cfloat a)
	{
		return f1 + a * (f2 - f1);
	}

	vec4 mix(cvec4 v1, cvec4 v2, cfloat a)
	{
		return v1 + (a * (v2 - v1));
	}

	quat mix(cquat & q1, cquat & q2, cfloat a)
	{
		cfloat cosTheta = dot(q1, q2);
		
		// Perform a linear interpolation when cosTheta is close to 1 to avoid side effect of sin(angle) becoming a zero denominator
		if (cosTheta > 1.f - FLT_EPSILON)
		{
			return quat(
				mix(q1.w, q2.w, a),
				mix(q1.x, q2.x, a),
				mix(q1.y, q2.y, a),
				mix(q1.z, q2.z, a)
			);
		}
		else
		{
			cfloat angle = acos(cosTheta);
			return (sin((1.f - a) * angle) * q1 + sin(a * angle) * q2) / sin(angle);
		}
	}

	quat lerp(cquat & q1, cquat & q2, cfloat a)
	{
		assert(a >= 0.f && a <= 1.f);
		return q1 * (1.f - a) + (q2 * a);
	}

	quat slerp(cquat & q1, cquat & q2, cfloat a)
	{
		quat q3(q2);

		float cosTheta = dot(q1, q2);

		// If cosTheta < 0, the interpolation will take the long way around the sphere.
		// To fix this, one quat must be negated.
		if (cosTheta < 0.f)
		{
			q3 = -q3;
			cosTheta = -cosTheta;
		}

		// Perform a linear interpolation when cosTheta is close to 1 to avoid side effect of sin(angle) becoming a zero denominator
		if (cosTheta > 1.f - FLT_EPSILON)
		{
			return quat(
				mix(q1.w, q3.w, a),
				mix(q1.x, q3.x, a),
				mix(q1.y, q3.y, a),
				mix(q1.z, q3.z, a)
			);
		}
		else
		{
			cfloat angle = acos(cosTheta);
			return (sin((1.f - a) * angle) * q1 + sin(a * angle) * q3) / sin(angle);
		}
	}

	float clamp(cfloat x, cfloat minX, cfloat maxX)
	{
		return minimum(maximum(x, minX), maxX);
	}

	void clamp(float * const x, cfloat minX, cfloat maxX)
	{
		*x = clamp(*x, minX, maxX);
	}

	float minimum(cfloat a, cfloat b)
	{
		return (b < a) ? b : a;
	}

	float maximum(cfloat a, cfloat b)
	{
		return (a < b) ? b : a;
	}
	vec3 minimum(cvec3& v1, cvec3& v2)
	{
		return vec3(
			minimum(v1.x, v2.x),
			minimum(v1.y, v2.y),
			minimum(v1.z, v2.z));
	}
	vec3 maximum(cvec3& v1, cvec3& v2)
	{
		return vec3(
			maximum(v1.x, v2.x),
			maximum(v1.y, v2.y),
			maximum(v1.z, v2.z));
	}

	float rand(cfloat a, cfloat b)
	{
		static auto seed = std::chrono::system_clock::now().time_since_epoch().count();
		static std::default_random_engine gen(static_cast<unsigned int>(seed));
		std::uniform_real_distribution<float> x(a, b);
		return x(gen);
	}

	float lerp(cfloat a, cfloat b, cfloat f)
	{
		return a + f * (b - a);
	}

	Transform::Transform() : _scale(1.0f), _rotation(quat::identity()), _position(0.0f)
	{ }

	vm::mat4 Transform::matrix()
	{
		return transform(_rotation, _scale, _position);
	}
}
