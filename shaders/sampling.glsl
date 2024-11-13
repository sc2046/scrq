// Common file shared between GLSL files.
#ifndef DEVICE_SAMPLING_H
#define DEVICE_SAMPLING_H

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


// Taken from "A Fast and Robust Method for Avoiding
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
// ONB
// ==============================================================

// Three ortho-normal vectors that form the basis for a local coordinate system
struct ONB
{
	vec3 s; // The tangent vector
	vec3 t; // The bi-tangent vector
	vec3 n; // The normal vector
};
 
// Generates an othonormal basis from a surface normal.
ONB generateONB(vec3 n)
{
	ONB onb;

	n = normalize(n);

	if (abs(n.x) > abs(n.y))
	{
		const float inv_len = 1.f / sqrt(n.x * n.x + n.z * n.z);
		vec3  y = vec3(n.z * inv_len, 0, -n.x * inv_len);
		onb.s = cross(y, n);
		onb.t = y;
		onb.n = n;
	}
	else
	{
		const float inv_len = 1.f / sqrt(n.y * n.y + n.z * n.z);
		vec3  y = vec3(0, n.z * inv_len, -n.y * inv_len);
		onb.s = cross(y, n);
		onb.t = y;
		onb.n = n;
	}
	return onb;
}

// Convert a vector from local coordinates to world coordinates.
vec3 toWorld(ONB onb, vec3 v)
{
	return v.x * onb.s + v.y * onb.t + v.z * onb.n;
}

// Convert a vector from world coordinates to local coordinates.
vec3 toLocal(vec3 v, ONB onb)
{
	return vec3(dot(onb.s, v), dot(onb.t, v), dot(onb.n, v));
}


// ==============================================================
// Sampling
// ==============================================================

/// Uniformly sample a vector within a 2D disk with radius 1, centered around the origin
vec2 sampleDisk(vec2 rv)
{
	const float r = sqrt(rv.y);
	const float sin_phi = sin(2.0f * M_PI * rv.x);
	const float cos_phi = sin(2.0f * M_PI * rv.x);

	return vec2(cos_phi * r, sin_phi * r);
}

/// Probability density of sampleDisk()
float sampleDiskPDF(vec2 p)
{
	return dot(p, p) <= 1 ? INV_PI : 0.f;
}

/// Uniformly sample a vector on the unit hemisphere around the pole (0,0,1) with respect to solid angles
vec3 sampleHemisphere(vec2 rv)
{
	const float cos_theta = 2.f * rv.x - 1;
	const float phi = 2.f * M_PI * rv.y;
	const float sin_theta = sqrt(1 - cos_theta * cos_theta);

	const float z = cos_theta < 0.f ? -cos_theta : cos_theta;
	return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, z);
}

/// Probability density of sampleHemisphere().
float sampleHemispherePDF(vec3 p)
{
	return INV_TWOPI;
}

/// Uniformly sample a vector on the unit hemisphere around the pole (0,0,1) with respect to projected solid angles.
vec3 sampleHemisphereCosine(vec2 rv)
{
	const float cos_theta = sqrt(rv.x);
	const float phi = 2.f * M_PI * rv.y;
	const float sin_theta = sqrt(1 - cos_theta * cos_theta);

	const float z = cos_theta < 0.f ? -cos_theta : cos_theta;
	return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, z);
}

/// Probability density of sample_hemisphere_cosine()
float sampleHemisphereCosinePDF(vec3 v)
{
	v = normalize(v);
	const float theta = acos(clamp(v.z, -1.f, 1.f));
	return cos(theta) * INV_PI;
}

/// Sample a vector on the unit hemisphere with a cosine-power density about the normal (0,0,1)
vec3 sampleHemisphereCosinePower(float exponent, vec2 rv)
{
	const float cos_theta = pow(rv[0], 1 / (exponent + 1));
	const float phi = 2.f * M_PI * rv[1];
	const float sin_theta = sqrt(1 - cos_theta * cos_theta);

	const float z = cos_theta < 0.f ? -cos_theta : cos_theta;
	return vec3(cos(phi) * sin_theta, sin(phi) * sin_theta, z);
}

/// Probability density of sampleHemisphereCosinePower()
float sampleHemisphereCosinePowerPDF(float exponent, float cosine)
{
	return (exponent + 1) * pow(cosine, exponent) * INV_TWOPI;
}


// ==============================================================
// Materials
// ==============================================================

/// 
/// Lambertian
/// 
bool sampleLambertian(vec3 inWorldO, vec3 inWorldD, HitInfo hitInfo, out vec3 attenuation, out vec3 scatterO, out vec3 scatterD, vec2 rv, float rv1)
{
	attenuation = hitInfo.albedo;

	const ONB onb = generateONB(hitInfo.sn);
	scatterD= normalize(toWorld(onb, sampleHemisphereCosine(rv)));
	scatterO = offsetPositionAlongNormal(hitInfo.p, hitInfo.sn);
	return true;
}

float pdfLambertian(vec3 inWorldO, vec3 inWorldD, vec3 scattered, HitInfo hitInfo)
{
	return max(0.f, dot(normalize(scattered), hitInfo.sn)) * INV_PI;
}

vec3 evalLambertian(vec3 inWorldO, vec3 inWorldD, vec3 scattered, HitInfo hitInfo)
{
	const vec3 albedo = hitInfo.albedo;
	return albedo * max(0.f, dot(normalize(scattered), hitInfo.sn)) * INV_PI;
}


/// 
/// Mirror (Smooth specular)
///
bool scatterMirror(vec3 inO, vec3 inD, HitInfo hitInfo, out vec3 attenuation, out vec3 scatterO, out vec3 scatterD, inout uint rngState)
{
	attenuation = hitInfo.albedo;

	const vec3 reflected = reflect(inD, hitInfo.gn);
	scatterD = normalize(reflected);
	scatterO = offsetPositionAlongNormal(hitInfo.p, hitInfo.gn);

	return true;
}

/// 
/// Dielectric
/// 
bool sampleDielectric(vec3 inWorldO, vec3 inWorldD, HitInfo hitInfo, out vec3 attenuation, out vec3 scatterO, out vec3 scatterD, vec2 rv, float rv1)
{
	attenuation = vec3(1.f, 1.f, 1.f);

	//TODO:
	const float ior = 1.5f;

	// Assume initially that ray is coming from outside the surface.
	float eta = 1.f / ior;
	vec3 n = hitInfo.gn;
	if (dot(inWorldD, hitInfo.gn) > 0.f) {
		// Ray is coming from inside the surface. Flip eta and reverse normal.
		eta = ior;
		n = -hitInfo.gn;
	}

	vec3 refracted;
	const bool can_refract = refract(normalize(inWorldD), normalize(n), eta, refracted);
	const float fresnelReflectance = schlick_reflectance(normalize(inWorldD), hitInfo.gn, 1.f, ior);

	if (!can_refract || fresnelReflectance > rv1) {
		scatterO = offsetPositionAlongNormal(hitInfo.p, n);
		scatterD = normalize(reflect(normalize(inWorldD), normalize(n)));
	}
	else {
		scatterO = offsetPositionAlongNormal(hitInfo.p, -n);
		scatterD = normalize(refracted);
	}
	return true;
}


///
/// PHONG
/// 
bool samplePhong(vec3 inWorldO, vec3 inWorldD, HitInfo hitInfo, out vec3 attenuation, out vec3 scatterO, out vec3 scatterD, vec2 rv, float rv1)
{
	attenuation = hitInfo.albedo;
	uint exponent = hitInfo.phongExponent;

	ONB onb = generateONB(normalize(reflect(normalize(inWorldD), hitInfo.sn)));
	scatterD = toWorld(onb, sampleHemisphereCosinePower(exponent, rv));
	scatterO = offsetPositionAlongNormal(hitInfo.p, hitInfo.sn);
	
	//Reject any invalid samples (i.e. those that don't lie in the hemisphere oriented at the surface normal)
	return dot(scatterD, hitInfo.sn) > 0.f;
}

float pdfPhong(vec3 inWorldO, vec3 inWorldD, vec3 scattered, HitInfo hitInfo)
{
	uint exponent = hitInfo.phongExponent;
	const vec3 mirror_dir = normalize(reflect(normalize(inWorldD), hitInfo.sn));
	const float cosine = max(dot(normalize(scattered), mirror_dir), 0.f);
	const float constant = (exponent + 1) * INV_TWOPI;
	return constant * pow(cosine, exponent);
}

vec3 evalPhong(vec3 inWorldO, vec3 inWorldD, vec3 scattered, HitInfo hitInfo)
{
	return hitInfo.albedo * pdfPhong(inWorldO, inWorldD, scattered, hitInfo);
}


#endif // #ifndef DEVICE_SAMPLING_H