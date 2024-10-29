// Common file shared between GLSL files.
#ifndef DEVICE_COMMON_H
#define DEVICE_COMMON_H

struct HitInfo
{
	float t;  // Ray parameter at the hit position.
	vec3 p;  // World-space hit position.
	vec3 gn; // Geometric normal.
	vec3 sn; // Interpolated shading normal (for triangles).
	//vec2 uv; // < UV texture coordinates
	vec3 color;
	//uint matID;
};


Ray generateRay(Camera cam, vec2 pixel, uvec2 resolution)
{
	Ray ray;

	ray.origin = cam.origin;
	const vec2 xy = (pixel - resolution / 2.f) / resolution.y;
	const float z = 1.f / (2.f * tan(radians(cam.fovY) / 2.f));
	ray.direction = normalize(vec3(xy.x, -xy.y, -z));
	return ray;
}


// ==============================================================
// Intersection Routines
// ==============================================================

// Returns the ray t-value at the intersection point, or -1 if no intersection occured.
float hitSphere(Sphere s, Ray r)
{
	const vec3  oc = r.origin - s.center;
	const float a = dot(r.direction, r.direction);
	const float b = 2.0 * dot(oc, r.direction);
	const float c = dot(oc, oc) - s.radius * s.radius;
	const float discriminant = b * b - 4 * a * c;

	return discriminant < 0.f ? -1.f : (-b - sqrt(discriminant)) / (2.0 * a);
}

// ==============================================================
// RNG
// ==============================================================

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
float stepAndOutputRNGFloat(inout uint rngState)
{
	// Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].

	rngState = rngState * 747796405 + 1;
	uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
	word = (word >> 22) ^ word;
	return float(word) / 4294967295.0f;
}

// ==============================================================
// Maths
// ==============================================================
bool refract(vec3 v_, vec3 n, float eta, inout vec3 refracted)
{
	vec3 v = normalize(v_);
	const float dt = dot(v, n);
	const float discrim = 1.f - eta * eta * (1.f - dt * dt);
	if (discrim > 0.f)
	{
		refracted = eta * (v - n * dt) - n * sqrt(discrim);
		return true;
	}
	else
		return false;
}

void swap(inout float a, inout float b) {
	float temp = a;
	a = b;
	b = temp;
}

float schlick_reflectance(vec3 v, vec3 n, float ext_ior, float int_ior)
{
	float cosi = dot(normalize(v), n);
	if (cosi > 0.f)
	{
		swap(ext_ior, int_ior);
	}
	const float sint = ext_ior / int_ior * sqrt(max(0.f, 1 - cosi * cosi));

	// Total internal reflection
	if (sint >= 1.f) {
		return 1.f;
	}

	else
	{
		float cost = sqrt(max(0.f, 1 - sint * sint));
		cosi = abs(cosi);
		float Rs = ((int_ior * cosi) - (ext_ior * cost)) / ((int_ior * cosi) + (ext_ior * cost));
		float Rp = ((ext_ior * cosi) - (int_ior * cost)) / ((ext_ior * cosi) + (int_ior * cost));
		return (Rs * Rs + Rp * Rp) / 2;
	}
}


// offsetPositionAlongNormal shifts a point on a triangle surface so that a
// ray bouncing off the surface with tMin = 0.0 is no longer treated as
// intersecting the surface it originated from.
//
// uses an improved technique by Carsten Wächter and
// Nikolaus Binder from "A Fast and Robust Method for Avoiding
// Self-Intersection" from Ray Tracing Gems (version 1.7, 2020).
// The normal can be negated if one wants the ray to pass through
// the surface instead.
vec3 offsetPositionAlongNormal(vec3 worldPosition, vec3 normal)
{
	// Convert the normal to an integer offset.
	const float int_scale = 256.0f;
	const ivec3 of_i = ivec3(int_scale * normal);

	// Offset each component of worldPosition using its binary representation.
	// Handle the sign bits correctly.
	const vec3 p_i = vec3(  //
		intBitsToFloat(floatBitsToInt(worldPosition.x) + ((worldPosition.x < 0) ? -of_i.x : of_i.x)),
		intBitsToFloat(floatBitsToInt(worldPosition.y) + ((worldPosition.y < 0) ? -of_i.y : of_i.y)),
		intBitsToFloat(floatBitsToInt(worldPosition.z) + ((worldPosition.z < 0) ? -of_i.z : of_i.z)));

	// Use a floating-point offset instead for points near (0,0,0), the origin.
	const float origin = 1.0f / 32.0f;
	const float floatScale = 1.0f / 65536.0f;
	return vec3(  //
		abs(worldPosition.x) < origin ? worldPosition.x + floatScale * normal.x : p_i.x,
		abs(worldPosition.y) < origin ? worldPosition.y + floatScale * normal.y : p_i.y,
		abs(worldPosition.z) < origin ? worldPosition.z + floatScale * normal.z : p_i.z);
}

// ==============================================================
// Materials
// ==============================================================

bool scatterDiffuse(Ray ray, HitInfo hitInfo, out vec3 attenuation, out Ray scattered, inout uint rngState)
{
	attenuation = hitInfo.color;

	// Avoid self-shadowing.
	scattered.origin = hitInfo.p - 0.0001f * sign(dot(ray.direction, hitInfo.gn)) * hitInfo.gn;

	const float theta = 2.f * M_PI * stepAndOutputRNGFloat(rngState);  // Random in [0, 2pi]
	const float u = 2.0 * stepAndOutputRNGFloat(rngState) - 1.0;   // Random in [-1, 1]
	const float r = sqrt(1.0 - u * u);
	const vec3  direction = hitInfo.gn + vec3(r * cos(theta), r * sin(theta), u);
	scattered.direction = normalize(direction);

	return true;
}

bool scatterMetal(Ray ray, HitInfo hitInfo, out vec3 attenuation, out Ray scattered, inout uint rngState)
{
	attenuation = hitInfo.color;

	// Avoid self-shadowing.
	scattered.origin = hitInfo.p - 0.0001f * sign(dot(ray.direction, hitInfo.gn)) * hitInfo.gn;

	vec3 reflected = reflect(ray.direction, hitInfo.gn);
	scattered.direction = normalize(reflected);

	return true;
}

bool scatterDielectric(Ray ray, HitInfo hitInfo, out vec3 attenuation, out Ray scattered, inout uint rngState)
{
	attenuation = vec3(1.f, 1.f, 1.f);

	//TODO:
	const float ior = 1.5f;

	// Assume initially that ray is coming from outside the surface.
	float eta = 1.f / ior;
	vec3 n = hitInfo.gn;
	if (dot(ray.direction, hitInfo.gn) > 0.f) {
		// Ray is coming from inside the surface. Flip eta and reverse normal.
		eta = ior;
		n =  -hitInfo.gn;
	}

	vec3 refracted;
	const bool can_refract		= refract(normalize(ray.direction), normalize(n), eta, refracted);
	const float fresnel			= schlick_reflectance(normalize(ray.direction), hitInfo.gn, 1.f, ior);

	if (!can_refract || fresnel > stepAndOutputRNGFloat(rngState)) {
		// TIR
		scattered.direction = normalize(reflect(normalize(ray.direction), normalize(n)));
	}
	else {
		scattered.direction = normalize(refracted);
	}
	scattered.origin = hitInfo.p;
	return true;
}

#endif // #ifndef SHADER_COMMON_H