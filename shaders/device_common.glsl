// Common file shared between GLSL files.
#ifndef DEVICE_COMMON_H
#define DEVICE_COMMON_H


/// Stores geometric and material information from a ray-surface intersection point.
struct HitInfo
{
	float t;  // Ray parameter at the hit position.
	vec3 p;  // World-space hit position.
	vec3 gn; // Geometric normal.
	vec3 sn; // Interpolated shading normal (for triangles).
	vec2 uv; // < UV texture coordinates

	uint materialType;
	vec3 albedo; 
	// vec3 albedoTextureID;
	vec3 emitted;
	int	phongExponent;

};

void generateRay(Camera cam, vec2 pixel, uvec2 resolution, out vec3 origin, out vec3 direction)
{
	const float image_plane_height = 2.f * cam.focalDistance * tan(radians(cam.fovY) / 2.f);
	const float image_plane_width = image_plane_height * (float(resolution.x) / resolution.y);

	// Define the ray in local coordinates;
	origin = vec3(0.f);
	const float pixel_width = image_plane_width / float(resolution.x);
	const float pixel_height = image_plane_height / float(resolution.y);
	direction.x = -image_plane_width / 2.f + pixel.x * pixel_width;
	direction.y = image_plane_height / 2.f - pixel.y * pixel_height;
	direction.z = -1.f;

	// Determine the world transform for the camera.
	const vec3 up = vec3(0, 1, 0);
	const vec3 w = normalize(cam.center - cam.eye);
	const vec3 u = normalize(cross(up, w));
	const vec3 v = normalize(cross(w, u));

	const mat4 transform = mat4(
		vec4(u, 0.f),
		vec4(v, 0.f),
		vec4(w, 0.f),
		vec4(cam.center, 1.f)
	);

	// Transform the ray into world space;
	const vec4 oTransform = transform * vec4(origin, 1.f);
	origin = oTransform.xyz / oTransform.w;

	const vec4 dTransform = transform * vec4(direction, 0.f);
	direction = normalize(dTransform.xyz);
}

// ==============================================================
// Intersection Routines
// Note: During ray-surface intersections, the ray is transformed 
// into the local space of the surface. 
// ==============================================================

bool hitSphere(vec3 localO, vec3 localD, mat4x3 worldToObject, mat4x3 objectToWorld, inout HitInfo hitInfo)
{

	const vec3 oc		= localO;
	const float a		= dot(localD,localD);
	const float half_b	= dot(oc, localD);
	const float c		= dot(oc,oc) - 1.f;	//	TODO: in local space, radius is always 1?

	const float discriminant = half_b * half_b - a * c;
	if (discriminant < 0.f) return false;

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

	// Hit info in object space.
	const float tHit	= root;					
	const vec3 p		= localO + tHit * localD;	
	const vec3 n		= p;					


	// Convert hit info to world space.
	hitInfo.t	= tHit;									//t is unchanged because linear maps preserve distances!
	hitInfo.p	= objectToWorld * vec4(p, 1.f);
	hitInfo.gn	= normalize((n * worldToObject).xyz);
	hitInfo.sn	= hitInfo.gn;								// For a sphere, the shading normal is the same as the geometric normal.

	//const auto& [phi, theta] = Spherical::direction_to_spherical_coordinates(normalize(n));
	//const Vec2f uv = Vec2f(phi * INV_TWOPI, theta * INV_PI);
	//hit.uv = uv;

	return true;
}


// ==============================================================
// RNG
// ==============================================================

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
// Uses a condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
float stepAndOutputRNGFloat(inout uint rngState)
{
	rngState = rngState * 747796405 + 1;
	uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
	word = (word >> 22) ^ word;
	return float(word) / 4294967295.0f;
}


// ==============================================================
// Integrators
// ==============================================================



#endif // #ifndef SHADER_COMMON_H