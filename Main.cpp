#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h" 

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <sstream>

// --- GLOBALE ZA REZOLUCIJU ---
float WINDOW_WIDTH = 800.0f;
float WINDOW_HEIGHT = 600.0f;
float PANEL_WIDTH = 0;

enum LiftState { IDLE, MOVING_UP, MOVING_DOWN, DOOR_OPENING, DOOR_OPEN, DOOR_CLOSING };

struct Button {
    float x, y, w, h;
    std::string label;
    bool isPressed;
    int floorIndex;
    int actionType; // 0=sprat, 1=otv, 2=zat, 3=stop, 4=vent
};

// Globalne promenljive
std::string floorNames[8] = { "SU", "PR", "1", "2", "3", "4", "5", "6" };
float liftY = 0;
int currentFloor = 2; // Krece sa 1. sprata (indeks 2)

// Globalne promenljive za dimenzije slike lifta
int liftImgWidth = 0;
int liftImgHeight = 0;

// Osoba
float personX = 0;
float personY = 0;
bool personInLift = false;

LiftState liftState = IDLE;
float doorHeight = 0.0f;
float MAX_DOOR_HEIGHT = 0.0f;

double doorOpenTimeStart = 0;
bool extendedOnce = false;
const double DOOR_DURATION = 5.0; // 5 sekundi
bool ventilationOn = false;

std::vector<Button> buttons;
bool floorRequests[8] = { false };

// Teksture
unsigned int buildingTexture;
unsigned int liftTexture;
unsigned int fanTexture; 

// --- POMOCNE FUNKCIJE ---
// 
// Vraca X koordinatu gde lift vizuelno pocinje (za detekciju ulaska)
float getLiftVisualX() {
    float buildingWidth = WINDOW_WIDTH * 0.3f;
    float buildingX = WINDOW_WIDTH - buildingWidth;
    float liftW = buildingWidth * 0.8f;
    float liftX = buildingX + (buildingWidth * 0.1f);
    return liftX;
}

// --- POMOCNA FUNKCIJA ZA DIMENZIJE ---
float getFloorH() { return WINDOW_HEIGHT / 8.0f; }

// Vraca X koordinatu i Sirinu lifta (ZALEPLJEN DESNO + PROPORCIONALAN)
void getLiftDimensions(float& outX, float& outW) {
    // 1. Racunamo visinu lifta (isto kao u updateApp)
    float fh = getFloorH();
    float liftH = fh * 0.9f;

    // 2. Racunamo sirinu na osnovu originalne slike (da se ne deformise)
    // (liftImgWidth i liftImgHeight su one globalne promenljive koje smo napunili pri ucitavanju)
    if (liftImgHeight > 0) {
        float aspectRatio = (float)liftImgWidth / (float)liftImgHeight;
        outW = liftH * aspectRatio;
    }
    else {
        outW = 100.0f; // Sigurnosna vrednost ako slika nije ucitana
    }

    // 3. Pozicija X: Skroz desno (Sirina prozora - Sirina lifta)
    outX = WINDOW_WIDTH - outW;
}

// --- TEXTURE LOADER ---
unsigned int loadTexture(char const* path) {
    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
    if (data) {
        GLenum format;
        if (nrComponents == 1) format = GL_RED;
        else if (nrComponents == 3) format = GL_RGB;
        else if (nrComponents == 4) format = GL_RGBA;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else {
        std::cout << "GRESKA: Tekstura nije ucitana sa putanje: " << path << std::endl;
        stbi_image_free(data);
    }
    return textureID;
}

// --- SHADER LOADER ---
unsigned int createShader(const char* vertexPath, const char* fragmentPath) {
    std::string vertexCode;
    std::string fragmentCode;
    std::ifstream vShaderFile;
    std::ifstream fShaderFile;
    vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    try {
        vShaderFile.open(vertexPath);
        fShaderFile.open(fragmentPath);
        std::stringstream vShaderStream, fShaderStream;
        vShaderStream << vShaderFile.rdbuf();
        fShaderStream << fShaderFile.rdbuf();
        vShaderFile.close();
        fShaderFile.close();
        vertexCode = vShaderStream.str();
        fragmentCode = fShaderStream.str();
    }
    catch (std::ifstream::failure& e) {
        std::cout << "SHADER ERROR: " << e.what() << std::endl;
    }
    const char* vShaderCode = vertexCode.c_str();
    const char* fShaderCode = fragmentCode.c_str();

    unsigned int vertex, fragment;
    int success; char infoLog[512];
    vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &vShaderCode, NULL);
    glCompileShader(vertex);
    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &fShaderCode, NULL);
    glCompileShader(fragment);
    unsigned int ID = glCreateProgram();
    glAttachShader(ID, vertex);
    glAttachShader(ID, fragment);
    glLinkProgram(ID);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return ID;
}

// --- VECTOR FONT ---
void appendChar(std::vector<float>& vertices, char c, float x, float y, float s) {
    auto addLine = [&](float x1, float y1, float x2, float y2) {
        vertices.push_back(x + x1 * s); vertices.push_back(y + y1 * s);
        vertices.push_back(x + x2 * s); vertices.push_back(y + y2 * s);
        };
    switch (toupper(c)) {
    case '0': addLine(0, 0, 1, 0); addLine(1, 0, 1, 2); addLine(1, 2, 0, 2); addLine(0, 2, 0, 0); break;
    case '1': addLine(0.5, 0, 0.5, 2); break;
    case '2': addLine(0, 2, 1, 2); addLine(1, 2, 1, 1); addLine(1, 1, 0, 1); addLine(0, 1, 0, 0); addLine(0, 0, 1, 0); break;
    case '3': addLine(0, 2, 1, 2); addLine(1, 2, 1, 0); addLine(1, 0, 0, 0); addLine(0, 1, 1, 1); break;
    case '4': addLine(0, 2, 0, 1); addLine(0, 1, 1, 1); addLine(1, 0, 1, 2); break;
    case '5': addLine(1, 2, 0, 2); addLine(0, 2, 0, 1); addLine(0, 1, 1, 1); addLine(1, 1, 1, 0); addLine(1, 0, 0, 0); break;
    case '6': addLine(1, 2, 0, 2); addLine(0, 2, 0, 0); addLine(0, 0, 1, 0); addLine(1, 0, 1, 1); addLine(1, 1, 0, 1); break;
    case 'S': addLine(1, 2, 0, 2); addLine(0, 2, 0, 1); addLine(0, 1, 1, 1); addLine(1, 1, 1, 0); addLine(1, 0, 0, 0); break;
    case 'U': addLine(0, 2, 0, 0); addLine(0, 0, 1, 0); addLine(1, 0, 1, 2); break;
    case 'P': addLine(0, 0, 0, 2); addLine(0, 2, 1, 2); addLine(1, 2, 1, 1); addLine(1, 1, 0, 1); break;
    case 'R': addLine(0, 0, 0, 2); addLine(0, 2, 1, 2); addLine(1, 2, 1, 1); addLine(0, 1, 1, 0); break;
    case 'O': addLine(0, 0, 1, 0); addLine(1, 0, 1, 2); addLine(1, 2, 0, 2); addLine(0, 2, 0, 0); break;
    case 'T': addLine(0.5, 0, 0.5, 2); addLine(0, 2, 1, 2); break;
    case 'V': addLine(0, 2, 0.5, 0); addLine(0.5, 0, 1, 2); break;
    case 'E': addLine(1, 0, 0, 0); addLine(0, 0, 0, 2); addLine(0, 2, 1, 2); addLine(0, 1, 1, 1); break;
    case 'N': addLine(0, 0, 0, 2); addLine(0, 2, 1, 0); addLine(1, 0, 1, 2); break;
    case 'Z': addLine(0, 2, 1, 2); addLine(1, 2, 0, 0); addLine(0, 0, 1, 0); break;
    case 'A': addLine(0, 0, 0, 2); addLine(0, 2, 1, 2); addLine(1, 2, 1, 0); addLine(0, 1, 1, 1); break;
    }
}

// --- INIT LOGIC ---
void initLogic() {
    buttons.clear();
    PANEL_WIDTH = WINDOW_WIDTH * 0.35f;

    float panelCenterX = PANEL_WIDTH / 2.0f;
    float startY = WINDOW_HEIGHT * 0.8f;
    float btnW = PANEL_WIDTH * 0.3f;
    float btnH = WINDOW_HEIGHT * 0.06f;
    float gapX = btnW * 0.2f;
    float gapY = btnH * 0.5f;

    // --- SPRATOVI ---
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 2; col++) {
            int logicIndex = 7 - (row * 2 + col);
            Button b;
            if (col == 0) b.x = panelCenterX - btnW - (gapX / 2);
            else          b.x = panelCenterX + (gapX / 2);
            b.y = startY - row * (btnH + gapY);
            b.w = btnW; b.h = btnH;
            b.label = floorNames[logicIndex];
            b.isPressed = floorRequests[logicIndex];
            b.floorIndex = logicIndex;
            b.actionType = 0;
            buttons.push_back(b);
        }
    }
    // --- SPECIJALNI TASTERI ---
    float specStartY = WINDOW_HEIGHT * 0.3f;
    std::string specs[] = { "OTVORI", "ZATVORI", "STOP", "VENT" };
    for (int i = 0; i < 4; i++) {
        Button b;
        if (i % 2 == 0) b.x = panelCenterX - btnW - (gapX / 2);
        else            b.x = panelCenterX + (gapX / 2);
        b.y = specStartY - (i / 2) * (btnH + gapY);
        b.w = btnW; b.h = btnH;
        b.label = specs[i];
        b.isPressed = false;
        b.floorIndex = -1;
        b.actionType = i + 1;
        buttons.push_back(b);
    }

    // Inicijalizacija POZICIJA prema zadatku
    static bool firstRun = true;
    if (firstRun) {
        float floorHeight = WINDOW_HEIGHT / 8.0f;

        // Zadatak: "Lift se na poèetku nalazi na prvom spratu." -> Index 2 (SU=0, PR=1, 1=2)
        currentFloor = 2;
        liftY = 2 * floorHeight;

        // Zadatak: "Osoba se na poèetku nalazi u prizemlju, van lifta." -> Index 1
        personY = 1 * floorHeight;
        personX = PANEL_WIDTH + 50; // Malo desno od panela
        personInLift = false;

        firstRun = false;
    }
    MAX_DOOR_HEIGHT = (WINDOW_HEIGHT / 8.0f) * 0.9f;
}

void checkRequests() {
    if (liftState != IDLE) return;
    int target = -1;
    int minDistance = 100;

    // Trazimo najblizi sprat koji je pozvan
    for (int i = 0; i < 8; i++) {
        if (floorRequests[i]) {
            int dist = abs(i - currentFloor);
            if (dist < minDistance) { minDistance = dist; target = i; }
        }
    }

    if (target != -1) {
        if (target > currentFloor) liftState = MOVING_UP;
        else if (target < currentFloor) liftState = MOVING_DOWN;
        else {
            // Ako smo vec tu, otvori vrata
            floorRequests[target] = false;
            liftState = DOOR_OPENING;
            if (ventilationOn) ventilationOn = false;
        }
    }
}

void updateApp() {
    float fh = getFloorH();
    float speed = fh * 0.02f; // Brzina kretanja

    if (liftState == MOVING_UP) {
        liftY += speed;
        if (liftY >= (currentFloor + 1) * fh) {
            currentFloor++;
            if (floorRequests[currentFloor]) {
                liftState = DOOR_OPENING;
                floorRequests[currentFloor] = false;
                if (ventilationOn) ventilationOn = false;
            }
            else { checkRequests(); if (liftState == IDLE) liftState = MOVING_DOWN; }
        }
    }
    else if (liftState == MOVING_DOWN) {
        liftY -= speed;
        if (liftY <= (currentFloor - 1) * fh) {
            currentFloor--;
            if (floorRequests[currentFloor]) {
                liftState = DOOR_OPENING;
                floorRequests[currentFloor] = false;
                if (ventilationOn) ventilationOn = false;
            }
            else { checkRequests(); }
        }
    }

    // Logika Vrata
    if (liftState == DOOR_OPENING) {
        doorHeight += speed * 0.5f;
        // U funkciji: void updateApp()
        // Deo: if (liftState == DOOR_OPENING) { ...

        if (doorHeight >= MAX_DOOR_HEIGHT) {
            doorHeight = MAX_DOOR_HEIGHT;
            liftState = DOOR_OPEN;
            doorOpenTimeStart = glfwGetTime(); // Poèni merenje 5s

            extendedOnce = false; // <--- NOVO: Resetujemo opciju za produženje

            // <--- NOVO: Kad se vrata otvore, zahtev je ispunjen, gasimo lampicu
            if (currentFloor >= 0 && currentFloor < 8) {
                floorRequests[currentFloor] = false;
            }
        }
    }
    else if (liftState == DOOR_CLOSING) {
        doorHeight -= speed * 0.5f;
        if (doorHeight <= 0) {
            doorHeight = 0;
            liftState = IDLE;
            checkRequests(); // Kad se zatvore, vidi gde dalje
        }
    }
    else if (liftState == DOOR_OPEN) {
        // Cekaj 5 sekundi
        if (glfwGetTime() - doorOpenTimeStart > DOOR_DURATION) {
            liftState = DOOR_CLOSING;
        }
    }

    // --- KLJUCNO: AZURIRANJE POZICIJE OSOBE ---
    // Ako je osoba u liftu, njena Y koordinata je uvek fiksirana za liftY
    if (personInLift) {
        personY = liftY + 5; // +5 da ne propadne kroz pod
    }
}

// --- INPUTS (Tastatura) ---
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        float moveSpeed = 10.0f;

        // 1. OSOBA JE VAN LIFTA
        if (!personInLift) {
            if (key == GLFW_KEY_A) { // Levo
                personX -= moveSpeed;
                if (personX < PANEL_WIDTH) personX = PANEL_WIDTH;
            }
            if (key == GLFW_KEY_W) { // Desno
                personX += moveSpeed;

                // === LOGIKA ULAZKA U LIFT (W) ===
                float liftStartX = getLiftVisualX();
                // Ako predjemo prag lifta DOK su vrata otvorena i lift je na nasem spratu
                if (personX >= liftStartX && liftState == DOOR_OPEN) {
                    int personFloor = (int)(personY / getFloorH());
                    if (personFloor == currentFloor) {
                        personInLift = true; // <--- OSOBA ULAZI
                        std::cout << "Usao u lift!" << std::endl;
                    }
                }

                // Granica je 90% sirine ekrana
                if (personX > WINDOW_WIDTH * 0.92f) personX = WINDOW_WIDTH * 0.92f;
            }

            // === LOGIKA POZIVANJA LIFTA (C) ===
                        // IZMJENA: Radi samo ako si presao 92% sirine ekrana (skroz desno)
            if (key == GLFW_KEY_C && personX >= (WINDOW_WIDTH * 0.92f)) {

                int personFloor = (int)(personY / getFloorH());
                std::cout << "Pozivam lift na sprat: " << personFloor << std::endl;

                // Ako je lift vec tu i otvoren, ne radi nista (koristi W za ulaz)
                if (!(liftState == DOOR_OPEN && currentFloor == personFloor)) {
                    floorRequests[personFloor] = true;
                    checkRequests();
                }
            }
        }
        // 2. OSOBA JE U LIFTU
        else {
            if (key == GLFW_KEY_A) {
                // === LOGIKA IZLASKA IZ LIFTA (A) ===
                // Izlazak moguc samo ako su vrata otvorena
                if (liftState == DOOR_OPEN) {
                    personInLift = false; // <--- OSOBA IZLAZI

                    // Izbaci osobu ispred lifta
                    personX = getLiftVisualX() - 30.0f;

                    // Postavi je na visinu trenutnog sprata
                    personY = currentFloor * getFloorH();
                    std::cout << "Izasao iz lifta na spratu: " << currentFloor << std::endl;
                }
            }
            // W i C ne rade nista dok si u liftu (vozis se)
        }
    }
}

// --- INPUTS (Mis) ---
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);
        float clickX = (float)x;
        float clickY = WINDOW_HEIGHT - (float)y;

        if (personInLift) {
            for (auto& b : buttons) {
                if (clickX >= b.x && clickX <= b.x + b.w && clickY >= b.y && clickY <= b.y + b.h) {

                    if (b.actionType == 0) { // SPRAT
                        b.isPressed = true;
                        floorRequests[b.floorIndex] = true;
                        checkRequests();
                    }
                    else if (b.actionType == 1) { // OTVORI
                        if (liftState == DOOR_OPEN && !extendedOnce) {
                            doorOpenTimeStart = glfwGetTime();
                            extendedOnce = true;
                            std::cout << "Vrata produzena!" << std::endl;
                        }
                    }
                    else if (b.actionType == 2) { // ZATVORI
                        if (liftState == DOOR_OPEN) liftState = DOOR_CLOSING;
                    }
                    else if (b.actionType == 3) { // STOP
                        liftState = IDLE;
                        for (int i = 0; i < 8; i++) floorRequests[i] = false;
                        for (auto& bb : buttons) bb.isPressed = false;
                    }
                    else if (b.actionType == 4) { // VENTILACIJA
                        // Samo menjamo bool vrednost, kursor sredjujemo u main-u
                        ventilationOn = !ventilationOn;
                    }
                }
            }
        }
    }
}

int endProgram(std::string message) {
    std::cout << message << std::endl;
    glfwTerminate();
    return -1;
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1000, 800, "Lift Projekat", NULL, NULL);
    if (window == NULL) return endProgram("Prozor nije uspeo da se kreira.");

    glfwMaximizeWindow(window);
    glfwMakeContextCurrent(window);

    if (glewInit() != GLEW_OK) return endProgram("GLEW nije uspeo da se inicijalizuje.");

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);

    unsigned int basicShader = createShader("basic.vert", "basic.frag");
    unsigned int textureShader = createShader("texture.vert", "texture.frag");

    buildingTexture = loadTexture("building.png");
    liftTexture = loadTexture("elevator.png");
    fanTexture = loadTexture("fan.png");

    int dummyComp;
    stbi_info("elevator.png", &liftImgWidth, &liftImgHeight, &dummyComp);

    // --- BAFERI ---
    float rectVertices[] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                             0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };
    unsigned int VAO_Rect, VBO_Rect;
    glGenVertexArrays(1, &VAO_Rect);
    glGenBuffers(1, &VBO_Rect);
    glBindVertexArray(VAO_Rect);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_Rect);
    glBufferData(GL_ARRAY_BUFFER, sizeof(rectVertices), rectVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    float texVertices[] = {
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,

        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f
    };
    unsigned int VAO_Tex, VBO_Tex;
    glGenVertexArrays(1, &VAO_Tex);
    glGenBuffers(1, &VBO_Tex);
    glBindVertexArray(VAO_Tex);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_Tex);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texVertices), texVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    unsigned int VAO_Line, VBO_Line;
    glGenVertexArrays(1, &VAO_Line);
    glGenBuffers(1, &VBO_Line);
    glBindVertexArray(VAO_Line);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_Line);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    unsigned int VAO_Fan, VBO_Fan;
    glGenVertexArrays(1, &VAO_Fan);
    glGenBuffers(1, &VBO_Fan);
    glBindVertexArray(VAO_Fan);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_Fan);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    int uResLoc = glGetUniformLocation(basicShader, "uRes");
    int uRectLoc = glGetUniformLocation(basicShader, "uRect");
    int uColorLoc = glGetUniformLocation(basicShader, "uColor");
    int uIsLineLoc = glGetUniformLocation(basicShader, "uIsLine");

    int uTexResLoc = glGetUniformLocation(textureShader, "uRes");
    int uTexRectLoc = glGetUniformLocation(textureShader, "uRect");

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    WINDOW_WIDTH = (float)width;
    WINDOW_HEIGHT = (float)height;
    glViewport(0, 0, width, height);

    glUseProgram(basicShader);
    glUniform2f(uResLoc, WINDOW_WIDTH, WINDOW_HEIGHT);
    glUseProgram(textureShader);
    glUniform2f(uTexResLoc, WINDOW_WIDTH, WINDOW_HEIGHT);
    glUniform1i(glGetUniformLocation(textureShader, "texture1"), 0);

    initLogic();

    bool firstLoop = true;

    while (!glfwWindowShouldClose(window))
    {
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0) { glfwPollEvents(); continue; }

        if (firstLoop || (float)width != WINDOW_WIDTH || (float)height != WINDOW_HEIGHT) {
            float scaleX = (float)width / WINDOW_WIDTH;
            float scaleY = (float)height / WINDOW_HEIGHT;
            WINDOW_WIDTH = (float)width;
            WINDOW_HEIGHT = (float)height;
            glViewport(0, 0, width, height);

            glUseProgram(basicShader);
            glUniform2f(uResLoc, WINDOW_WIDTH, WINDOW_HEIGHT);
            glUseProgram(textureShader);
            glUniform2f(uTexResLoc, WINDOW_WIDTH, WINDOW_HEIGHT);

            if (!firstLoop) {
                liftY *= scaleY;
                personY *= scaleY;
                float oldPersonX = personX;
                personX *= scaleX;
            }

            PANEL_WIDTH = WINDOW_WIDTH * 0.35f;
            if (personX < PANEL_WIDTH) personX = PANEL_WIDTH + 10;
            initLogic();
            firstLoop = false;
        }

        updateApp();
        glfwPollEvents();

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // 1. PANEL
        glUseProgram(basicShader);
        glBindVertexArray(VAO_Rect);
        glUniform1i(uIsLineLoc, 0);
        glUniform4f(uColorLoc, 0.2f, 0.22f, 0.25f, 1.0f);
        glUniform4f(uRectLoc, 0, 0, PANEL_WIDTH, WINDOW_HEIGHT);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 2. DUGMAD
        for (auto& b : buttons) {

            // OVO JE NOVO: Sinhronizujemo izgled dugmeta sa logikom zahteva
            if (b.actionType == 0) {
                b.isPressed = floorRequests[b.floorIndex];
            }

            // A) POZADINA DUGMETA
            //glUseProgram(basicShader);
            //glBindVertexArray(VAO_Rect);
            //glUniform1i(uIsLineLoc, 0);

            //if (b.actionType == 4 && ventilationOn) glUniform4f(uColorLoc, 0.0f, 0.8f, 0.8f, 1.0f);
            //else glUniform4f(uColorLoc, 0.4f, 0.4f, 0.45f, 1.0f);

            //glUniform4f(uRectLoc, b.x, b.y, b.w, b.h);
            //glDrawArrays(GL_TRIANGLES, 0, 6);

            glUseProgram(basicShader);
            glBindVertexArray(VAO_Rect);
            glUniform1i(uIsLineLoc, 0);

            if (b.actionType == 4 && ventilationOn) glUniform4f(uColorLoc, 0.0f, 0.8f, 0.8f, 1.0f);
            else glUniform4f(uColorLoc, 0.4f, 0.4f, 0.45f, 1.0f); // Uvek siva pozadina za spratove

            glUniform4f(uRectLoc, b.x, b.y, b.w, b.h);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            // --- IZMENA 2: OKVIR (Beli ili Crni) ---
            std::vector<float> outline;
            // ... (kod za punjenje vektora outline sa 4 linije oko dugmeta) ...
            outline.push_back(b.x); outline.push_back(b.y);
            outline.push_back(b.x + b.w); outline.push_back(b.y);
            outline.push_back(b.x + b.w); outline.push_back(b.y);
            outline.push_back(b.x + b.w); outline.push_back(b.y + b.h);
            outline.push_back(b.x + b.w); outline.push_back(b.y + b.h);
            outline.push_back(b.x); outline.push_back(b.y + b.h);
            outline.push_back(b.x); outline.push_back(b.y + b.h);
            outline.push_back(b.x); outline.push_back(b.y);

            glBindVertexArray(VAO_Line);
            glBindBuffer(GL_ARRAY_BUFFER, VBO_Line);
            glBufferData(GL_ARRAY_BUFFER, outline.size() * sizeof(float), outline.data(), GL_DYNAMIC_DRAW);
            glUniform1i(uIsLineLoc, 1);

            // AKO JE AKTIVNO -> BELA BOJA, INAÈE CRNA
            if (b.isPressed && b.actionType == 0) {
                glUniform4f(uColorLoc, 1.0f, 1.0f, 1.0f, 1.0f); // BELA
                glLineWidth(2.0f); // Deblji okvir
            }
            else {
                glUniform4f(uColorLoc, 0.0f, 0.0f, 0.0f, 1.0f); // CRNA
                glLineWidth(1.0f);
            }
            glDrawArrays(GL_LINES, 0, (GLsizei)outline.size() / 2);

            // B) OKVIR DUGMETA
            outline.push_back(b.x); outline.push_back(b.y);
            outline.push_back(b.x + b.w); outline.push_back(b.y);

            outline.push_back(b.x + b.w); outline.push_back(b.y);
            outline.push_back(b.x + b.w); outline.push_back(b.y + b.h);

            outline.push_back(b.x + b.w); outline.push_back(b.y + b.h);
            outline.push_back(b.x); outline.push_back(b.y + b.h);

            outline.push_back(b.x); outline.push_back(b.y + b.h);
            outline.push_back(b.x); outline.push_back(b.y);

            glBindVertexArray(VAO_Line);
            glBindBuffer(GL_ARRAY_BUFFER, VBO_Line);
            glBufferData(GL_ARRAY_BUFFER, outline.size() * sizeof(float), outline.data(), GL_DYNAMIC_DRAW);
            glUniform1i(uIsLineLoc, 1);

            // Ako je pritisnuto -> BELI OKVIR
            if (b.isPressed && b.actionType == 0) {
                glUniform4f(uColorLoc, 1.0f, 1.0f, 1.0f, 1.0f); // BELA
                glLineWidth(2.0f); // Malo deblje
            }
            else {
                // Ako nije -> CRNI OKVIR
                glUniform4f(uColorLoc, 0.0f, 0.0f, 0.0f, 1.0f); // CRNA
                glLineWidth(1.0f);
            }
            glDrawArrays(GL_LINES, 0, (GLsizei)outline.size() / 2);

            // C) UNUTRASNJOST (Mali kvadrat unutra za lepsi izgled)
            glUseProgram(basicShader);
            glBindVertexArray(VAO_Rect);
            glUniform1i(uIsLineLoc, 0);

            if (b.actionType == 4 && ventilationOn) glUniform4f(uColorLoc, 0.0f, 0.8f, 0.8f, 1.0f);
            else glUniform4f(uColorLoc, 0.4f, 0.4f, 0.45f, 1.0f);

            glUniform4f(uRectLoc, b.x + 2, b.y + 2, b.w - 4, b.h - 4);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // 3. ZGRADA
        glUseProgram(textureShader);
        glBindVertexArray(VAO_Tex);

        float buildingWidth = WINDOW_WIDTH * 0.3f;
        float buildingX = WINDOW_WIDTH - buildingWidth;
        float fh = getFloorH();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, buildingTexture);
        glUniform4f(uTexRectLoc, buildingX, 0, buildingWidth, WINDOW_HEIGHT);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 4. LIFT KABINA
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, liftTexture);

        // Racunamo dimenzije lifta preko helper funkcije
        float liftX, liftW;
        getLiftDimensions(liftX, liftW);

        // Visina je 90% visine sprata
        float liftH = fh * 0.9f;

        glUniform4f(uTexRectLoc, liftX, liftY, liftW, liftH);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 5. OSOBA
        glUseProgram(basicShader);
        glBindVertexArray(VAO_Rect);

        if (personInLift) {
            glUniform4f(uColorLoc, 0.0f, 1.0f, 0.0f, 1.0f);
            // Osoba je u sredini lifta
            glUniform4f(uRectLoc, liftX + liftW / 2 - 15, liftY + 5, 30, fh * 0.6f);
        }
        else {
            glUniform4f(uColorLoc, 1.0f, 0.2f, 0.2f, 1.0f);
            glUniform4f(uRectLoc, personX, personY, 30, fh * 0.6f);
        }
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 6. VRATA (Plavi pravougaonik) - Manja i centrirana
        // Ne menjamo boju vrata

        // Crtamo vrata samo ako nisu skroz otvorena da bi se videla osoba kad su otvorena?
        // Ne, vrata se dizu, pa se osoba vidi ispod njih.

        glUniform4f(uColorLoc, 0.4f, 0.8f, 1.0f, 1.0f);
        float doorRectW = liftW * 0.4f;
        float doorRectH = fh * 0.7f;
        float doorRectX = liftX + (liftW - doorRectW) / 2.0f;
        float currentDoorY = liftY + doorHeight;

        glUniform4f(uRectLoc, doorRectX, currentDoorY, doorRectW, doorRectH);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 7. LINIJE I TEKST
        std::vector<float> lines;

        for (int i = 0; i < 8; i++) {
            float y = i * fh;
            float tx = buildingX - 30;
            float ty = y + fh / 2 - 5;
            for (char c : floorNames[i]) { appendChar(lines, c, tx, ty, fh * 0.08f); tx += fh * 0.13f; }
        }

        for (auto& b : buttons) {
            float charSize = b.h * 0.15f;
            float textLen = b.label.length() * (charSize + 6.0f);
            float tx = b.x + (b.w - textLen) / 2 + 5;
            float ty = b.y + (b.h / 2) - 5;
            for (char c : b.label) { appendChar(lines, c, tx, ty, charSize); tx += 8.0f; }
        }

        if (ventilationOn) {
            double cx, cy; glfwGetCursorPos(window, &cx, &cy);
            float mx = (float)cx; float my = WINDOW_HEIGHT - (float)cy;
            float angle = (float)glfwGetTime() * 8.0f;
            float s = sin(angle) * 20, c = cos(angle) * 20;
            lines.push_back(mx - c); lines.push_back(my - s);
            lines.push_back(mx + c); lines.push_back(my + s);
            lines.push_back(mx - s); lines.push_back(my + c);
            lines.push_back(mx + s); lines.push_back(my - c);
        }

        glBindVertexArray(VAO_Line);
        glBindBuffer(GL_ARRAY_BUFFER, VBO_Line);
        glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(float), lines.data(), GL_DYNAMIC_DRAW);
        glUniform1i(uIsLineLoc, 1);
        glUniform4f(uColorLoc, 0.0f, 0.0f, 0.0f, 1.0f);
        glDrawArrays(GL_LINES, 0, (GLsizei)lines.size() / 2);

        if (ventilationOn) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        }
        else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        // 8. VENTILATOR KAO KURSOR (SLIKA)
        if (ventilationOn) {
            glUseProgram(textureShader);
            // !! BITNO: Koristimo NOVI VAO_Fan, tako da NE diramo VAO_Tex koji koristi zgrada
            glBindVertexArray(VAO_Fan);

            double cx, cy; glfwGetCursorPos(window, &cx, &cy);
            float mx = (float)cx;
            float my = WINDOW_HEIGHT - (float)cy;

            float angle = (float)glfwGetTime() * 10.0f;
            float s = sin(angle);
            float c = cos(angle);

            float size = 50.0f;
            float halfS = size / 2.0f;

            float p1x = -halfS, p1y = -halfS;
            float p2x = halfS, p2y = -halfS;
            float p3x = halfS, p3y = halfS;
            float p4x = -halfS, p4y = halfS;

            float r1x = p1x * c - p1y * s; float r1y = p1x * s + p1y * c;
            float r2x = p2x * c - p2y * s; float r2y = p2x * s + p2y * c;
            float r3x = p3x * c - p3y * s; float r3y = p3x * s + p3y * c;
            float r4x = p4x * c - p4y * s; float r4y = p4x * s + p4y * c;

            float finalVertices[] = {
                mx + r1x, my + r1y,  0.0f, 0.0f,
                mx + r2x, my + r2y,  1.0f, 0.0f,
                mx + r3x, my + r3y,  1.0f, 1.0f,

                mx + r1x, my + r1y,  0.0f, 0.0f,
                mx + r3x, my + r3y,  1.0f, 1.0f,
                mx + r4x, my + r4y,  0.0f, 1.0f
            };

            // Ovde sada punimo VBO_Fan, a ne VBO_Tex!
            glBindBuffer(GL_ARRAY_BUFFER, VBO_Fan);
            glBufferData(GL_ARRAY_BUFFER, sizeof(finalVertices), finalVertices, GL_DYNAMIC_DRAW);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, fanTexture);

            glUniform4f(uTexRectLoc, 0.0f, 0.0f, 1.0f, 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // KONTROLA KURSORA
        if (ventilationOn) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        }
        else {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }

        glfwSwapBuffers(window);
    }
    glfwTerminate();
    return 0;
}