#pragma once

struct GLFWwindow;

namespace squarestar::platform {

void CenterGlfwWindowInWorkArea(GLFWwindow* window, int width, int height);
void ApplyFixedGlfwWindowLayout(GLFWwindow* window, int width, int height);
void ApplyResizableGlfwWindowLayout(GLFWwindow* window,
                                    int width,
                                    int height,
                                    int minimumWidth,
                                    int minimumHeight);

}
