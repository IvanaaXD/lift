#version 330 core
// Pozicija (x, y)
layout (location = 0) in vec2 aPos;
// Koordinate teksture (u, v) - govore koji dio slike ide na koji dio coska
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform vec2 uRes;
uniform vec4 uRect; // x, y, width, height

void main()
{
    // Skaliranje kocke 0-1 na zeljenu velicinu i poziciju
    vec2 scaledPos = aPos * uRect.zw + uRect.xy;

    // Konverzija iz piksela [0, width] u OpenGL koordinate [-1, 1]
    vec2 clipPos = (scaledPos / uRes) * 2.0 - 1.0;

    gl_Position = vec4(clipPos.x, clipPos.y, 0.0, 1.0);

    // Prosljedjujemo koordinate teksture u fragment shader
    TexCoord = aTexCoord;
}