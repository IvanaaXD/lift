#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

// "Sampler" je objekat koji cita boju sa slike
uniform sampler2D texture1;

void main()
{
    // Uzmi boju sa teksture na koordinatama TexCoord
    FragColor = texture(texture1, TexCoord);
}