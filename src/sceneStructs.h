#pragma once

#include <string>
#include <vector>
#include <cuda_runtime.h>
#include "glm/glm.hpp"

using namespace std;
#define BACKGROUND_COLOR (glm::vec3(0.0f))
//#define MAX_RAY_TRACE_DEPTH (5)

enum GeomType {
    SPHERE,
    CUBE,
	TRIANGLE
};

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

struct Geom {
    enum GeomType type;
    int materialid;
    glm::vec3 translation;
    glm::vec3 rotation;
    glm::vec3 scale;
    glm::mat4 transform;
    glm::mat4 inverseTransform;
    glm::mat4 invTranspose;
};

struct Material {
    glm::vec3 color;
    struct {
        float exponent;
        glm::vec3 color;
    } specular;
    float hasReflective;
    float hasRefractive;
    float indexOfRefraction;
    float emittance;
};

struct Camera {
    glm::ivec2 resolution;
    glm::vec3 position;
    glm::vec3 view;
    glm::vec3 up;
    glm::vec2 fov;

	glm::vec3 right;	// same in x direction of the screen
	
	glm::vec2 pixelLength;


	///////
	// lens camera
	////////
	float lensRadiaus;
	float focalDistance;
};

struct RenderState {
    Camera camera;
    unsigned int iterations;
    int traceDepth;
    std::vector<glm::vec3> image;
    std::string imageName;
};



//MY

struct Path
{
	Ray ray;
	glm::vec3 color;

	int image_index;
	bool terminated;
};



enum AXIS { AXIS_X = 0, AXIS_Y, AXIS_Z};

struct AAPlane
{	
	AXIS axis;
	float pos;
};

struct AABB
{
	glm::vec3 min_pos;
	glm::vec3 max_pos;
};





