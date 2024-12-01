#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rs232.h"
#include "serial.h"

#define bdrate 115200          /* 115200 baud */
#define MAX_STROKES 50         // Maximum strokes per character
#define MAX_CHARACTERS 128     // Maximum ASCII characters
#define LINE_WIDTH 100.0f      // Maximum width of each line (in mm)
#define CHARACTER_SPACING 0.5f // Space between letters (in mm)
#define WORD_SPACING 7.0f      // Additional space between words (in mm)

// Define the FontChar structure to store stroke data
struct FontChar {
    int asciiCode;            // ASCII code of the character
    int strokeCount;          // Number of strokes
    float x[MAX_STROKES];     // X coordinates of the strokes
    float y[MAX_STROKES];     // Y coordinates of the strokes
    int draw[MAX_STROKES];    // Pen state (0 = up, 1 = down)
};

typedef struct FontChar FontChar;

// Function prototypes
int readFontData(const char *filename, FontChar fontDataArray[]);
void generateGCodeForText(FontChar fontDataArray[], const char *textFileName, float scalingFactor);
void sendGCodeCommand(const char *command);
void SendCommands(char *buffer);

// Provide a definition for `SendCommands` if it is not linked
void SendCommands(char *buffer) {
    // Simulate sending G-code to the robot
    printf("%s", buffer);
}

// Main function
int main() {
    FontChar fontDataArray[MAX_CHARACTERS] = {0}; // Initialize font data array
    char buffer[100];

    // Open the RS232 port
    if (CanRS232PortBeOpened() == -1) {
        printf("\nUnable to open the COM port (specified in serial.h)");
        exit(0);
    }

    // Wake up the robot
    printf("\nAbout to wake up the robot\n");
    sprintf(buffer, "\n");
    PrintBuffer(&buffer[0]);
    Sleep(100);
    WaitForDollar();

    printf("\nThe robot is now ready to draw\n");

    // Initialize the robot
    sendGCodeCommand("G1 X0 Y0 F1000\n");
    sendGCodeCommand("M3\n");
    sendGCodeCommand("S0\n");

    // Prompt user for character height
    float scalingFactor;
    printf("Enter the height for the characters (4-10 mm): ");
    scanf("%f", &scalingFactor);
    if (scalingFactor < 4.0f || scalingFactor > 10.0f) {
        printf("Error: Height must be between 4 and 10 mm.\n");
        CloseRS232Port();
        return 1;
    }
    scalingFactor /= 18.0f; // Calculate the scaling factor (height / 18)

    // Load font data
    if (!readFontData("SingleStrokeFont.txt", fontDataArray)) {
        printf("Error: Failed to read font data.\n");
        CloseRS232Port();
        return 1;
    }

    // Prompt user for text file name
    char textFileName[100];
    printf("Enter the name of the text file to read: ");
    scanf("%s", textFileName);

    // Generate G-code for the text
    generateGCodeForText(fontDataArray, textFileName, scalingFactor);

    // Close the RS232 port
    CloseRS232Port();
    printf("COM port now closed\n");
    return 0;
}

// Function to read font data
int readFontData(const char *filename, FontChar fontDataArray[]) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Error: Unable to open file %s.\n", filename);
        return 0;
    }

    char line[256];
    int currentChar = -1;
    int numStrokes = 0;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "999", 3) == 0) {
            if (sscanf(line, "999 %d %d", &currentChar, &numStrokes) != 2) {
                printf("Error: Invalid '999' line format.\n");
                fclose(file);
                return 0;
            }
            fontDataArray[currentChar].asciiCode = currentChar;
            fontDataArray[currentChar].strokeCount = numStrokes;
        } else if (currentChar != -1 && numStrokes > 0) {
            float x, y;
            int pen;
            if (sscanf(line, "%f %f %d", &x, &y, &pen) != 3) {
                printf("Error: Invalid stroke data format.\n");
                fclose(file);
                return 0;
            }
            int strokeIndex = fontDataArray[currentChar].strokeCount - numStrokes;
            fontDataArray[currentChar].x[strokeIndex] = x;
            fontDataArray[currentChar].y[strokeIndex] = y;
            fontDataArray[currentChar].draw[strokeIndex] = pen;
            numStrokes--;
        }
    }
    fclose(file);
    return 1;
}

// Function to generate G-code for the text
void generateGCodeForText(FontChar fontDataArray[], const char *textFileName, float scalingFactor) {
    FILE *file = fopen(textFileName, "r");
    if (!file) {
        printf("Error: Unable to open text file %s.\n", textFileName);
        return;
    }

    char word[100];
    float x_offset = 0.0f, y_offset = -(scalingFactor * 18.0f + 5.0f); // Start below zero to avoid positive Y-axis

    while (fscanf(file, "%s", word) != EOF) {
        if (strlen(word) >= 100) {
            printf("Error: Word length exceeds buffer size.\n");
            continue; // Skip long words
        }

        float wordWidth = 0.0f;
        for (int i = 0; word[i] != '\0'; i++) {
            int charIndex = word[i];
            wordWidth += fontDataArray[charIndex].x[fontDataArray[charIndex].strokeCount - 1] * scalingFactor;
            wordWidth += CHARACTER_SPACING * scalingFactor;
        }

        // Check if word fits on the current line
        if (x_offset + wordWidth > LINE_WIDTH) {
            y_offset -= (scalingFactor * 18.0f + 5.0f); // Move to the next line
            x_offset = 0.0f;
        }

        // Generate G-code for each character in the word
        for (int i = 0; word[i] != '\0'; i++) {
            int charIndex = word[i];
            FontChar currentChar = fontDataArray[charIndex];
            for (int j = 0; j < currentChar.strokeCount; j++) {
                float x = currentChar.x[j] * scalingFactor + x_offset;
                float y = currentChar.y[j] * scalingFactor + y_offset;
                int pen = currentChar.draw[j];
                char gcode[50];
                if (pen == 1) {
                    sprintf(gcode, "S1000\nG1 X%.2f Y%.2f\n", x, y);
                } else {
                    sprintf(gcode, "S0\nG0 X%.2f Y%.2f\n", x, y);
                }
                SendCommands(gcode);
            }
            x_offset += (currentChar.x[currentChar.strokeCount - 1] + CHARACTER_SPACING) * scalingFactor;
        }
        x_offset += WORD_SPACING * scalingFactor; // Add space between words
    }

    fclose(file);

    // Ensure the pen ends at the "pen up" position at (0,0)
    char finalGcode[50];
    sprintf(finalGcode, "S0\nG0 X0.00 Y0.00\n");
    SendCommands(finalGcode);
}

// Wrapper function to send G-code commands to the robot
void sendGCodeCommand(const char *command) {
    char buffer[100];
    sprintf(buffer, "%s", command);
    SendCommands(buffer);
}
