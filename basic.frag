#version 330 core
out vec4 FragColor;

uniform vec4 uColor; // Boja koju saljemo iz C++

void main()
{
    FragColor = uColor;
}