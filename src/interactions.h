#pragma once

#include "intersections.h"

#define RAY_EPSILON 0.001f
// CHECKITOUT
/**
 * Computes a cosine-weighted random direction in a hemisphere.
 * Used for diffuse lighting.
 */
__host__ __device__
glm::vec3 calculateRandomDirectionInHemisphere(
        glm::vec3 normal, thrust::default_random_engine &rng) {
    thrust::uniform_real_distribution<float> u01(0, 1);

    float up = sqrt(u01(rng)); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = u01(rng) * TWO_PI;

    // Find a direction that is not the normal based off of whether or not the
    // normal's components are all equal to sqrt(1/3) or whether or not at
    // least one component is less than sqrt(1/3). Learned this trick from
    // Peter Kutz.

    glm::vec3 directionNotNormal;
    if (abs(normal.x) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(1, 0, 0);
    } else if (abs(normal.y) < SQRT_OF_ONE_THIRD) {
        directionNotNormal = glm::vec3(0, 1, 0);
    } else {
        directionNotNormal = glm::vec3(0, 0, 1);
    }

    // Use not-normal direction to generate two perpendicular directions
    glm::vec3 perpendicularDirection1 =
        glm::normalize(glm::cross(normal, directionNotNormal));
    glm::vec3 perpendicularDirection2 =
        glm::normalize(glm::cross(normal, perpendicularDirection1));

    return up * normal
        + cos(around) * over * perpendicularDirection1
        + sin(around) * over * perpendicularDirection2;
}

/**
 * Scatter a ray with some probabilities according to the material properties.
 * For example, a diffuse surface scatters in a cosine-weighted hemisphere.
 * A perfect specular surface scatters in the reflected ray direction.
 * In order to apply multiple effects to one surface, probabilistically choose
 * between them.
 * 
 * The visual effect you want is to straight-up add the diffuse and specular
 * components. You can do this in a few ways. This logic also applies to
 * combining other types of materias (such as refractive).
 * 
 * - Always take an even (50/50) split between a each effect (a diffuse bounce
 *   and a specular bounce), but divide the resulting color of either branch
 *   by its probability (0.5), to counteract the chance (0.5) of the branch
 *   being taken.
 *   - This way is inefficient, but serves as a good starting point - it
 *     converges slowly, especially for pure-diffuse or pure-specular.
 * - Pick the split based on the intensity of each material color, and divide
 *   branch result by that branch's probability (whatever probability you use).
 *
 * This method applies its changes to the Ray parameter `ray` in place.
 * It also modifies the color `color` of the ray in place.
 *
 * You may need to change the parameter list for your purposes!
 */
__host__ __device__
void scatterRay(
        Ray &ray,
        glm::vec3 &color,
        glm::vec3 intersect,
        glm::vec3 normal,
        const Material &m,
        thrust::default_random_engine &rng) 
{
    // TODO: implement this.
    // A basic implementation of pure-diffuse shading will just call the
    // calculateRandomDirectionInHemisphere defined above.
	
	


	float schlickR = 1;

	
	int num_ray_type = 0;
	int scatter_type_array[3];

	//diffuse
	if (!m.hasReflective && !m.hasRefractive)
	{
		scatter_type_array[num_ray_type] = 0;
		num_ray_type++;
	}

	if(m.hasReflective)
	{
		scatter_type_array[num_ray_type] = 1;
		num_ray_type++;
	}

	if(m.hasRefractive)
	{
		scatter_type_array[num_ray_type] = 2;
		num_ray_type++;
	}

	//TODO: other ray type (sub scatter...)


	int scatter_ray_type = scatter_type_array[0];
	//random choose;
	if(num_ray_type > 1)
	{
		thrust::uniform_int_distribution<int> uirand(0, num_ray_type-1);
		scatter_ray_type = scatter_type_array[uirand(rng)];
	}
	
	


	
	if(scatter_ray_type == 2)
	{
		//refractive
		

		float cosi = glm::dot(-ray.direction, normal);
		float sini = sqrtf(1 - cosi*cosi);
		float ni, nt;

		float normal_sign;
		
		if (cosi > 0)
		{
			//from outside
			ni = 1;
			nt = m.indexOfRefraction;
			normal_sign = 1;
		}
		else
		{
			ni = m.indexOfRefraction;
			nt = 1;
			normal_sign = -1;
		}

		float sint = ni * sini / nt;
		float R0 = (ni - nt) / (ni + nt);
		R0 *= R0;
		schlickR = R0 + (1 - R0) * powf(1 - cosi, 5);
		if (sint > 1)
		{
			//total reflect
			num_ray_type--;
			scatter_ray_type = 1;

			
		}
		else
		{
			float cost = sqrtf(1 - sint*sint);
			color *= m.color*(float)num_ray_type * (1 - schlickR);
			glm::vec3 horizontal = (ray.direction + normal * normal_sign * cosi) / sini;
			ray.direction = horizontal * sint - normal * normal_sign * cost;
			//ray.direction = ray.direction * ni / nt + normal * normal_sign * (ni / nt*cosi - cost);
			ray.origin = intersect - normal * normal_sign * RAY_EPSILON;
		}
	}


	if (scatter_ray_type == 0)
	{
		//diffuse
		ray.direction = calculateRandomDirectionInHemisphere(normal, rng);
		//color *= m.color * glm::dot( ray.direction, normal) * (float)num_ray_type;
		color *= m.color  * (float)num_ray_type;
		ray.origin = intersect + normal * RAY_EPSILON;

	}
	else if (scatter_ray_type == 1)
	{
		//reflective
		color *= m.specular.color * (float)num_ray_type * schlickR;
		ray.direction = ray.direction + 2 * glm::dot(-ray.direction, normal) * normal;
		ray.origin = intersect + normal * RAY_EPSILON;
	}
	
	
}
