#version 330 core
layout (location = 0) in vec2 aPos; // Ulazni verteks (0..1 za kocke, ili tacan put za linije)

uniform vec2 uRes;       // Rezolucija ekrana (800, 800)
uniform vec4 uRect;      // Za iscrtavanje kvadrata: x, y, width, height (ako crtamo linije, ovo ignorisemo ili podesimo drugacije)
uniform bool uIsLine;    // Da li crtamo linije (text/spratovi) ili kvadrate

void main()
{
    vec2 pos;
    
    if (!uIsLine) {
        // Skaliranje i translacija jediniènog kvadrata
        pos = aPos * uRect.zw + uRect.xy; 
    } else {
        // Ako su linije, aPos su vec stvarne koordinate
        pos = aPos;
    }

    // Konverzija iz (0..Width, 0..Height) u (-1..1, -1..1)
    vec2 ndc = (pos / uRes) * 2.0 - 1.0;
    
    // GLFW obicno ima (0,0) dole levo, ali ako zelis da y=0 bude gore, invertuj y ovde.
    // Tvoj zadatak logicnije radi ako je y=0 dole (standardni koordinatni sistem).
    gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
}