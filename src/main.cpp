#include "main.h"
#include "preview.h"
#include <src/scene.h>
#include <cstring>

#include <stream_compaction/stream_compaction.h>

//#define STREAM_COMPACTION_TEST

static std::string startTimeString;
static bool camchanged = false;
static float theta = 0, phi = 0;
static glm::vec3 cammove;
//Scene scene;
Scene *scene;
RenderState *renderState;
int iteration;

int width;
int height;


//MY
void sceneInitKDTree()
{
	std::cout << "init kd tree..." << std::endl;
	scene->kdtree.init(*scene);

	std::cout << "done!" << std::endl;
	
	//std::cout << sizeof(Node) << std::endl;
	//std::cout << (scene->kdtree.last_idx) << std::endl;
	//std::cout << scene->kdtree.hst_node.size() << std::endl;
}


//-------------------------------
//-------------MAIN--------------
//-------------------------------

int main(int argc, char** argv) {


	//std::cout<<sizeof(char)<<std::endl;
	//std::cout<<sizeof(int)<<std::endl;
	//std::cout<<sizeof(glm::vec3)<<std::endl;
	//std::cout<<sizeof(Ray)<<std::endl;
	//std::cout<<sizeof(Path)<<std::endl;
	//std::cout<<sizeof(Geom)<<std::endl;
	//std::cout<<sizeof(Material)<<std::endl;
	
	Scene myScene;
	scene = &myScene;


#ifdef STREAM_COMPACTION_TEST

	//test stream compaction
	int num = 15000;
	//int num = 30;
	std::vector<Path> hos_paths(num);
	std::vector<Path> hos_paths_cmp(num);


	//init
	int ii = 0;
	for (Path& p : hos_paths)
	{
		p.terminated = (ii%11!=0);
		p.image_index = ii;
		ii++;
	}
	hos_paths.at(num - 1).terminated = false;;
	//hos_paths.at(0).terminated = false;
	//hos_paths.at(1).terminated = false;
	//hos_paths.at(5).terminated = false;
	//hos_paths.at(10).terminated = false;
	//hos_paths.at(13).terminated = false;
	//hos_paths.at(16).terminated = false;
	//hos_paths.at(21).terminated = false;

	//hos_paths.at(38).terminated = false;
	//hos_paths.at(99).terminated = false;
	//hos_paths.at(177).terminated = false;
	//hos_paths.at(199).terminated = false;

	////////

	int cpu_compact_num = StreamCompaction::Efficient::compactWithoutScan(num, hos_paths_cmp.data(), hos_paths.data());
	std::cout << "input:" << std::endl;
	StreamCompaction::Efficient::printArray(num, hos_paths.data());
	std::cout << "cpu:" << std::endl;
	StreamCompaction::Efficient::printArray(cpu_compact_num, hos_paths_cmp.data());


	Path * dev_paths;
	cudaMalloc(&dev_paths, num*sizeof(Path));
	cudaMemcpy(dev_paths, hos_paths.data(), num*sizeof(Path), cudaMemcpyHostToDevice);

	num = StreamCompaction::Efficient::compact(num, dev_paths);


	cudaMemcpy(hos_paths.data(), dev_paths, num*sizeof(Path), cudaMemcpyDeviceToHost);


	cudaFree(dev_paths);

	//StreamCompaction::Efficient::printArray(cpu_compact_num, hos_paths_cmp.data());
	
	std::cout << "gpu:" << std::endl;
	StreamCompaction::Efficient::printArray(num, hos_paths.data());


	
	if (cpu_compact_num != num)
	{
		printf("fail,num not equal... cpu:%d       gpu:%d\n",cpu_compact_num,num);
	}
	else if (StreamCompaction::Efficient::cmpArrays(num, hos_paths_cmp.data(), hos_paths.data()))
	{
		printf("fail\n");
	}

	return 0;
	////////////////////////////////////

#endif




    startTimeString = currentTimeString();

    if (argc < 2) {
        printf("Usage: %s SCENEFILE.txt\n", argv[0]);
        return 1;
    }

    const char *sceneFile = argv[1];

    // Load scene file
    //scene = new Scene(sceneFile);
	scene->loadScene(sceneFile);
#ifdef USE_KDTREE_FLAG
	sceneInitKDTree();
#endif

    // Set up camera stuff from loaded path tracer settings
    iteration = 0;
    renderState = &scene->state;
	//renderState = &scene.state;
    width = renderState->camera.resolution.x;
    height = renderState->camera.resolution.y;

    // Initialize CUDA and GL components
    init();

    // GLFW main loop
    mainLoop();

	//delete scene;

    return 0;
}

void saveImage() {
    float samples = iteration;
    // output image file
    image img(width, height);

    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            int index = x + (y * width);
            glm::vec3 pix = renderState->image[index];
            img.setPixel(width - 1 - x, y, glm::vec3(pix) / samples);
        }
    }

    std::string filename = renderState->imageName;
    std::ostringstream ss;
    ss << filename << "." << startTimeString << "." << samples << "samp";
    filename = ss.str();

    // CHECKITOUT
    img.savePNG(filename);
    //img.saveHDR(filename);  // Save a Radiance HDR file
}




void runCuda() {
    if (camchanged) {
        iteration = 0;
        Camera &cam = renderState->camera;
        glm::vec3 v = cam.view;
        glm::vec3 u = cam.up;
        glm::vec3 r = glm::cross(v, u);
        glm::mat4 rotmat = glm::rotate(theta, r) * glm::rotate(phi, u);
        cam.view = glm::vec3(rotmat * glm::vec4(v, 0.f));
        cam.up = glm::vec3(rotmat * glm::vec4(u, 0.f));
        cam.position += cammove.x * r + cammove.y * u + cammove.z * v;
        theta = phi = 0;
        cammove = glm::vec3();
        camchanged = false;
    }

    // Map OpenGL buffer object for writing from CUDA on a single GPU
    // No data is moved (Win & Linux). When mapped to CUDA, OpenGL should not use this buffer

    if (iteration == 0) {
		pathtraceFree();
		
		
        pathtraceInit(scene);
    }
	

    if (iteration < renderState->iterations) {
        uchar4 *pbo_dptr = NULL;
        iteration++;
        cudaGLMapBufferObject((void**)&pbo_dptr, pbo);
		
        // execute the kernel
        int frame = 0;
        pathtrace(pbo_dptr, frame, iteration);
		
        // unmap buffer object
        cudaGLUnmapBufferObject(pbo);
    } else {
        saveImage();
        pathtraceFree();
        cudaDeviceReset();
        exit(EXIT_SUCCESS);
    }
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_ESCAPE:
            saveImage();
            glfwSetWindowShouldClose(window, GL_TRUE);
            break;
        case GLFW_KEY_SPACE:
            saveImage();
            break;
        case GLFW_KEY_DOWN:  camchanged = true; theta = -0.1f; break;
        case GLFW_KEY_UP:    camchanged = true; theta = +0.1f; break;
        case GLFW_KEY_RIGHT: camchanged = true; phi = -0.1f; break;
        case GLFW_KEY_LEFT:  camchanged = true; phi = +0.1f; break;
        case GLFW_KEY_A:     camchanged = true; cammove -= glm::vec3(.1f, 0, 0); break;
        case GLFW_KEY_D:     camchanged = true; cammove += glm::vec3(.1f, 0, 0); break;
        case GLFW_KEY_W:     camchanged = true; cammove += glm::vec3(0, 0, .1f); break;
        case GLFW_KEY_S:     camchanged = true; cammove -= glm::vec3(0, 0, .1f); break;
        case GLFW_KEY_R:     camchanged = true; cammove += glm::vec3(0, .1f, 0); break;
        case GLFW_KEY_F:     camchanged = true; cammove -= glm::vec3(0, .1f, 0); break;
        }
    }
}
