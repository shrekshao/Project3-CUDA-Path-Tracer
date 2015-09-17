#include <cstdio>
#include <cuda.h>
#include <cmath>
#include <thrust/execution_policy.h>
#include <thrust/random.h>
#include <thrust/remove.h>

#include <thrust/device_vector.h>


#include "sceneStructs.h"
#include "scene.h"
#include "glm/glm.hpp"
#include "glm/gtx/norm.hpp"
#include "utilities.h"
#include "pathtrace.h"
#include "intersections.h"
#include "interactions.h"

#define FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#define checkCUDAError(msg) checkCUDAErrorFn(msg, FILENAME, __LINE__)
void checkCUDAErrorFn(const char *msg, const char *file, int line) {
    cudaError_t err = cudaGetLastError();
    if (cudaSuccess == err) {
        return;
    }

    fprintf(stderr, "CUDA error");
    if (file) {
        fprintf(stderr, " (%s:%d)", file, line);
    }
    fprintf(stderr, ": %s: %s\n", msg, cudaGetErrorString(err));
    exit(EXIT_FAILURE);
}

__host__ __device__ thrust::default_random_engine random_engine(
        int iter, int index = 0, int depth = 0) {
    return thrust::default_random_engine(utilhash((index + 1) * iter) ^ utilhash(depth));
}

//Kernel that writes the image to the OpenGL PBO directly.
__global__ void sendImageToPBO(uchar4* pbo, glm::ivec2 resolution,
        int iter, glm::vec3* image) {
    int x = (blockIdx.x * blockDim.x) + threadIdx.x;
    int y = (blockIdx.y * blockDim.y) + threadIdx.y;

    if (x < resolution.x && y < resolution.y) {
        int index = x + (y * resolution.x);
        glm::vec3 pix = image[index];

        glm::ivec3 color;
        color.x = glm::clamp((int) (pix.x / iter * 255.0), 0, 255);
        color.y = glm::clamp((int) (pix.y / iter * 255.0), 0, 255);
        color.z = glm::clamp((int) (pix.z / iter * 255.0), 0, 255);

        // Each thread writes one pixel location in the texture (textel)
        pbo[index].w = 0;
        pbo[index].x = color.x;
        pbo[index].y = color.y;
        pbo[index].z = color.z;
    }
}

static Scene *hst_scene = NULL;
static glm::vec3 *dev_image = NULL;
// TODO: static variables for device memory, scene/camera info, etc
// ...

static Ray * dev_ray0;
static Ray * dev_ray1;

//static thrust::device_vector<Ray> * dev_ray0;

static Ray * dev_ray_cur;
static Ray * dev_ray_next;

//static thrust::device_vector<Geom> dev_geom;			//global memory
//static thrust::device_vector<Material> dev_material;	//global
static Geom * dev_geom;
static Material * dev_material;


void pathtraceInit(Scene *scene) {
    hst_scene = scene;
    const Camera &cam = hst_scene->state.camera;
    const int pixelcount = cam.resolution.x * cam.resolution.y;

    cudaMalloc(&dev_image, pixelcount * sizeof(glm::vec3));
    cudaMemset(dev_image, 0, pixelcount * sizeof(glm::vec3));
    // TODO: initialize the above static variables added above

	dev_ray_cur = dev_ray0;
	dev_ray_next = dev_ray1;

	cudaMalloc(&dev_ray_cur, pixelcount * sizeof(Ray));

	//dev_geom = scene->geoms;
	//dev_material = scene->materials;

	cudaMalloc(&dev_geom, scene->geoms.size() * sizeof (Geom));

	cudaMemcpy(dev_geom, scene->geoms.data() , scene->geoms.size() * sizeof (Geom), cudaMemcpyHostToDevice);
	//Geom * d = dev_geom;
	//for (std::vector<Geom>::iterator it = scene->geoms.begin() ; it != scene->geoms.end(); ++it) {
	//	int *src = &((*it)[0]);
	//	size_t sz = sizeof (Geom);

	//	cudaMemcpy(d, src, sizeof(int)*sz, cudaMemcpyHostToDevice);
	//	d += sz;
	//}
	


    checkCUDAError("pathtraceInit");
}

void pathtraceFree() {
    cudaFree(dev_image);  // no-op if dev_image is null
    // TODO: clean up the above static variables

	cudaFree(dev_ray0);
	cudaFree(dev_ray1);

	cudaFree(dev_geom);

    checkCUDAError("pathtraceFree");
}

/**
 * Example function to generate static and test the CUDA-GL interop.
 * Delete this once you're done looking at it!
 */
__global__ void generateNoiseDeleteMe(Camera cam, int iter, glm::vec3 *image) {
    int x = (blockIdx.x * blockDim.x) + threadIdx.x;
    int y = (blockIdx.y * blockDim.y) + threadIdx.y;

    if (x < cam.resolution.x && y < cam.resolution.y) {
        int index = x + (y * cam.resolution.x);

        thrust::default_random_engine rng = random_engine(iter, index, 0);
        thrust::uniform_real_distribution<float> u01(0, 1);

        // CHECKITOUT: Note that on every iteration, noise gets added onto
        // the image (not replaced). As a result, the image smooths out over
        // time, since the output image is the contents of this array divided
        // by the number of iterations.
        //
        // Your renderer will do the same thing, and, over time, it will become
        // smoother.
        image[index] += glm::vec3(u01(rng));
    }
}




__host__ __device__ void getCemeraRayAtPixel(Ray & ray,const Camera &c, int x, int y,int iter,int index)
{
	thrust::default_random_engine rng = random_engine(iter, index, 0);
	thrust::uniform_real_distribution<float> u01(0, 1);
	Ray r;
	r.origin = c.position;
	r.direction = glm::normalize(  c.view * 0.5f * (float)c.resolution.y / c.fov.y
		+ c.right * ((float)x - (float)c.resolution.x / 2.0f + u01(rng) )		//u01(rng) is for jiitering for antialiasing
		- c.up * ((float)y - (float)c.resolution.y / 2.0f + u01(rng) )			//u01(rng) is for jiitering for antialiasing
		);

	r.image_index = index;

	//TODO: lens effect
}


/**
 * Generate Rays from camera through screen to the field
 * which is the first generation of rays
 *
 * Antialiasing - num of rays per pixel
 * motion blur - jitter scene position
 * lens effect - jitter camera position
 */
__global__ void generateRayFromCamera(Camera cam, int iter, Ray* rays)
{
	int x = (blockIdx.x * blockDim.x) + threadIdx.x;
    int y = (blockIdx.y * blockDim.y) + threadIdx.y;

    if (x < cam.resolution.x && y < cam.resolution.y) {
        int index = x + (y * cam.resolution.x);
		Ray & ray = rays[index];
		getCemeraRayAtPixel(ray,cam,x,y,iter,index);

		//TODO: k-d tree accleration goes here?

       
    }
}



__global__ void pathTraceOneBounce(int depth,int num_rays,glm::vec3 * image
										,Ray * rays
										,Geom * geoms, int geoms_size,Material * materials, int materials_size
										//,const thrust::device_vector<Geom> & geoms , const thrust::device_vector<Material> & materials
										)
{
	int blockId = blockIdx.x + blockIdx.y * gridDim.x;
	int ray_index = blockId * (blockDim.x * blockDim.y) + (threadIdx.y * blockDim.x) + threadIdx.x;
	
	if(ray_index < num_rays)
	{
		Ray & ray = rays[ray_index];
		//calculate intersection
		float t;
		glm::vec3 intersect_point;
		glm::vec3 normal;
		//naive parse through global geoms
		//for ( thrust::device_vector<Geom>::iterator it = geoms.begin(); it != geoms.end(); ++it)
		for(int i = 0; i < geoms_size; i++)
		{
			//Geom & geom = static_cast<Geom>(*it);
			Geom & geom = geoms[i];
			if( geom.type == CUBE)
			{
				t = boxIntersectionTest(geom,ray,intersect_point,normal);
			}
			else if( geom.type == SPHERE)
			{
				t = sphereIntersectionTest(geom,ray,intersect_point,normal);
			}
			else
			{
				//TODO: triangle
				printf("ERROR: geom type error at %d\n",i);
			}
		}


		if(t < -0.9f)
		{
			ray.terminated = false;
			image[ray.image_index] += BACKGROUND_COLOR;
			//image[ray.image_index] += glm::vec3(1.0f);
		}
		else
		{
			//TODO: BSDF

			//TODO:scatter ray

			//test: paint it white
			image[ray.image_index] += glm::vec3(1.0f);
		}
	}
}




/**
 * Wrapper for the __global__ call that sets up the kernel calls and does a ton
 * of memory management
 */
void pathtrace(uchar4 *pbo, int frame, int iter) {
    const int traceDepth = hst_scene->state.traceDepth;
    const Camera &cam = hst_scene->state.camera;
    const int pixelcount = cam.resolution.x * cam.resolution.y;

    const int blockSideLength = 8;
    const dim3 blockSize(blockSideLength, blockSideLength);
    const dim3 blocksPerGrid(
            (cam.resolution.x + blockSize.x - 1) / blockSize.x,
            (cam.resolution.y + blockSize.y - 1) / blockSize.y);

    ///////////////////////////////////////////////////////////////////////////

    // Recap:
    // * Initialize array of path rays (using rays that come out of the camera)
    //   * You can pass the Camera object to that kernel.
    //   * Each path ray is a (ray, color) pair, where color starts as the
    //     multiplicative identity, white = (1, 1, 1).
    //   * For debugging, you can output your ray directions as colors.
    // * For each depth:
    //   * Compute one new (ray, color) pair along each path - note
    //     that many rays will terminate by hitting a light or nothing at all.
    //     You'll have to decide how to represent your path rays and how
    //     you'll mark terminated rays.
    //     * Color is attenuated (multiplied) by reflections off of any object
    //       surface.
    //     * You can debug your ray-scene intersections by displaying various
    //       values as colors, e.g., the first surface normal, the first bounced
    //       ray direction, the first unlit material color, etc.
    //   * Add all of the terminated rays' results into the appropriate pixels.
    //   * Stream compact away all of the terminated paths.
    //     You may use either your implementation or `thrust::remove_if` or its
    //     cousins.
    // * Finally, handle all of the paths that still haven't terminated.
    //   (Easy way is to make them black or background-colored.)

    // TODO: perform one iteration of path tracing


    //generateNoiseDeleteMe<<<blocksPerGrid, blockSize>>>(cam, iter, dev_image);

	int depth = 0;

	generateRayFromCamera<<<blocksPerGrid,blockSize>>>(cam,iter,dev_ray_cur);
	checkCUDAError("generate camera ray");

	//test
	
	pathTraceOneBounce<<<blocksPerGrid,blockSize>>>(depth,pixelcount,dev_image, dev_ray_cur
		, dev_geom, hst_scene->geoms.size(), dev_material, hst_scene->materials.size());
	checkCUDAError("trace first bounce");
	depth ++;


    ///////////////////////////////////////////////////////////////////////////

    // Send results to OpenGL buffer for rendering
    sendImageToPBO<<<blocksPerGrid, blockSize>>>(pbo, cam.resolution, iter, dev_image);

    // Retrieve image from GPU
    cudaMemcpy(hst_scene->state.image.data(), dev_image,
            pixelcount * sizeof(glm::vec3), cudaMemcpyDeviceToHost);

    checkCUDAError("pathtrace");
}
