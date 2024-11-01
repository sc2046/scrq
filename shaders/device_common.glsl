// Common file shared between GLSL files.
#ifndef DEVICE_COMMON_H
#define DEVICE_COMMON_H


/// Stores information from a ray-surface intersection point.
/// 
/// 
/// 
struct HitInfo
{
	float t;  // Ray parameter at the hit position.
	vec3 p;  // World-space hit position.
	vec3 gn; // Geometric normal.
	vec3 sn; // Interpolated shading normal (for triangles).
	//vec2 uv; // < UV texture coordinates
	
	uint materialType;
	vec3 albedo; //Replacement for uv coordinates until textures are added.
	//Material material;
};


 void generateRay(Camera cam, vec2 pixel, uvec2 resolution, out vec3 origin, out vec3 direction)
{
	const float image_plane_height	= 2.f * cam.focalDistance * tan(radians(cam.fovY) / 2.f);
	const float image_plane_width	= image_plane_height * (float(resolution.x) / resolution.y);

	// Define the ray in local coordinates;
	origin = vec3(0.f);
	const float pixel_width		= image_plane_width / float(resolution.x);
	const float pixel_height	= image_plane_height/ float(resolution.y);
	direction.x = -image_plane_width / 2.f + pixel.x * pixel_width;
	direction.y =  image_plane_height / 2.f - pixel.y * pixel_height;
	direction.z = -1.f;

	// Determine the world transform for the camera.
	const vec3 up	= vec3(0, 1, 0);
	const vec3 w	= normalize(cam.center - cam.eye);
	const vec3 u	= normalize(cross(up, w));
	const vec3 v	= normalize(cross(w, u));
	
	const mat4 transform = mat4(
		vec4(u, 0.f),
		vec4(v, 0.f),
		vec4(w, 0.f),
		vec4(cam.center, 1.f)
	);

	// Transform the ray into world space;
	const vec4 oTransform	= transform * vec4(origin, 1.f);
	origin					= oTransform.xyz / oTransform.w;

	const vec4 dTransform	= transform * vec4(direction, 0.f);
	direction				= normalize(dTransform.xyz);
}

// ==============================================================
// Intersection Routines
// Note: During ray-surface intersections, the ray is transformed 
// into the local space of the surface. 
// ==============================================================

bool hitSphere(vec3 worldO, vec3 worldD, mat4x3 worldToObject, mat4x3 objectToWorld, inout HitInfo hitInfo)
{
	const vec3 localO = worldToObject * vec4(worldO, 1.0f);
	const vec3 localD = normalize(worldToObject * vec4(worldD, 0.f));

	const vec3 oc = localO;
	const float a = dot(localD,localD);
	const float half_b = dot(oc, localD);
	const float c = dot(oc,oc) - 1.f;	//	TODO: in local space, radius is always 1?

	const float discriminant = half_b * half_b - a * c;
	if (discriminant < 0) return false;

	// Find the nearest root that lies in the acceptable range.
	float sqrtd = sqrt(discriminant);
	float root = (-half_b - sqrtd) / a;
	if (root <= 0.001f || root >= hitInfo.t)
	{
		//Try other root
		root = (-half_b + sqrtd) / a;
		if (root <= 0.001f || root >= hitInfo.t)
			return false;
	}

	const float tHit	= root;					// Ray parameter in object space
	const vec3 p		= localO + tHit * localD;	// Hit point in local space
	const vec3 n		= p;					// Surface normal in local space

	hitInfo.t	= tHit;									//t is unchanged because linear maps preserve distances!
	hitInfo.p	= worldToObject * vec4(localO, 1.f);
	hitInfo.gn	= normalize((n * worldToObject).xyz);
	hitInfo.sn	= hitInfo.gn;								// For a sphere, the shading normal is the same as the geometric normal.
	//hitInfo.color = hitInfo.sn;

	//const auto& [phi, theta] = Spherical::direction_to_spherical_coordinates(normalize(n));
	//const Vec2f uv = Vec2f(phi * INV_TWOPI, theta * INV_PI);
	//hit.uv = uv;

	return true;
}


/// Intersects a ray against a sphere.
/// If a valid intersection was found, fills the hitInfo struct, and returns true.
/// Otherwise, returns false.
bool hitSphere(Sphere s, vec3 worldRayO, vec3 worldRayD, inout HitInfo hitInfo)
{
	const vec3  oc = worldRayO - s.center;
	const float a = dot(worldRayD, worldRayD);
	const float b = 2.0 * dot(oc, worldRayD);
	const float c = dot(oc, oc) - s.radius * s.radius;
	const float discriminant = b * b - 4 * a * c;

	if (discriminant < 0.f)
		return false;

	const float sqrtd = sqrt(discriminant);
	float tHit = (-b - sqrtd) / (2.f * a);
	if (tHit <= 0.001f || tHit >= hitInfo.t)
	{
		//Try other root
		tHit = (-b + sqrtd) / a;
		if (tHit <= 0.001f || tHit >= hitInfo.t)
			return false;
	}

	hitInfo.t = tHit;
	hitInfo.p = worldRayO + tHit * worldRayD;
	hitInfo.gn = normalize(hitInfo.p - s.center);
	hitInfo.sn = hitInfo.gn;
	//hitInfo.material = s.material;
	//hitInfo.color = s.color;

	return true;
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

bool scatterDiffuse(vec3 inO, vec3 inD, HitInfo hitInfo, out vec3 attenuation, out vec3 scatterO, out vec3 scatterD, inout uint rngState)
{
	attenuation = hitInfo.albedo;

	// Avoid self-shadowing.
	scatterO = offsetPositionAlongNormal(hitInfo.p, hitInfo.gn);

	const float theta = 2.f * M_PI * stepAndOutputRNGFloat(rngState);  // Random in [0, 2pi]
	const float u = 2.0 * stepAndOutputRNGFloat(rngState) - 1.0;   // Random in [-1, 1]
	const float r = sqrt(1.0 - u * u);
	const vec3  direction = hitInfo.gn + vec3(r * cos(theta), r * sin(theta), u);
	scatterD = normalize(direction);

	return true;
}

bool scatterMetal(vec3 inO, vec3 inD, HitInfo hitInfo, out vec3 attenuation, out vec3 scatterO, out vec3 scatterD, inout uint rngState)
{
	attenuation = hitInfo.albedo;

	// Avoid self-shadowing.
	scatterO = offsetPositionAlongNormal(hitInfo.p, hitInfo.gn);

	const vec3 reflected = reflect(inD, hitInfo.gn);
	scatterD = normalize(reflected);

	return true;
}

bool scatterDielectric(vec3 inO, vec3 inD, HitInfo hitInfo, out vec3 attenuation, out vec3 scatterO, out vec3 scatterD, inout uint rngState)
{
	attenuation = vec3(1.f, 1.f, 1.f);

	//TODO:
	const float ior = 1.5f;

	// Assume initially that ray is coming from outside the surface.
	float eta = 1.f / ior;
	vec3 n = hitInfo.gn;
	if (dot(inD, hitInfo.gn) > 0.f) {
		// Ray is coming from inside the surface. Flip eta and reverse normal.
		eta = ior;
		n =  -hitInfo.gn;
	}

	vec3 refracted;
	const bool can_refract		= refract(normalize(inD), normalize(n), eta, refracted);
	const float fresnel			= schlick_reflectance(normalize(inD), hitInfo.gn, 1.f, ior);

	if (!can_refract || fresnel > stepAndOutputRNGFloat(rngState)) {
		scatterO = offsetPositionAlongNormal(hitInfo.p, n);
		scatterD= normalize(reflect(normalize(inD), normalize(n)));
	}
	else {
		scatterO = offsetPositionAlongNormal(hitInfo.p, -n);
		scatterD = normalize(refracted);
	}
	return true;
}

#endif // #ifndef SHADER_COMMON_H