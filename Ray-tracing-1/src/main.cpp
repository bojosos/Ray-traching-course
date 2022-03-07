#include <fstream>
#include <iostream>

static const int imageWidth = 512;
static const int imageHeight = 512;

static const int maxColorComponent = 255;

static const int rectangleCount = 6;

struct color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

struct vec2 {
	uint32_t x, y;
};

void WriteRectangles()
{
	std::ofstream ppmFileStream("rectangles.ppm", std::ios::out | std::ios::binary);
	ppmFileStream << "P3\n";
	ppmFileStream << imageWidth << " " << imageHeight << "\n";
	ppmFileStream << maxColorComponent << "\n";

	const int xStep = imageWidth / rectangleCount;
	const int yStep = imageHeight / rectangleCount;
	color col { 0, 0 ,0 };

	for (int rowIdx = 0; rowIdx < imageHeight; ++rowIdx) {
		for (int colIdx = 0; colIdx < imageWidth; ++colIdx) {
			int rowRectIdx = rowIdx / yStep + 1;
			int colRectIdx = colIdx / xStep + 1;
			col.r = 255 / rowRectIdx;
			col.g = 255 / colRectIdx;
			col.b = 255 / (rowRectIdx + colRectIdx) * 2;
			ppmFileStream << (uint32_t)col.r << ' ' << (uint32_t)col.g << ' ' << (uint32_t)col.b << '\t';
		}
		ppmFileStream << "\n";
	}

	ppmFileStream.close();
}

void DrawTriangle(color* buffer, const vec2& p1, const vec2& p2, const vec2& p3)
{
	float invslope1 = ((float)p2.x - p1.x) / (p2.y - p1.y);
	float invslope2 = ((float)p3.x - p1.x) / (p3.y - p1.y);

	float curx1 = p1.x;
	float curx2 = p1.x;

	for (int scanlineY = p1.y; scanlineY <= p2.y; scanlineY++)
	{
		int yOff = scanlineY * imageWidth;
		for (int i = curx1; i <= curx2; i++)
			buffer[yOff + i] = { 220, 20, 60 };
		curx1 += invslope1;
		curx2 += invslope2;
	}
}

void WriteTriangle()
{
	std::ofstream ppmFileStream("triangle.ppm", std::ios::out | std::ios::binary);
	ppmFileStream << "P3\n";
	ppmFileStream << imageWidth << " " << imageHeight << "\n";
	ppmFileStream << maxColorComponent << "\n";

	color* buffer = new color[imageWidth * imageHeight];

	color background{ 120, 240, 69 };
	color foreground{ 220, 20, 60 };
	for (int rowIdx = 0; rowIdx < imageHeight; ++rowIdx)
		for (int colIdx = 0; colIdx < imageWidth; ++colIdx)
			buffer[rowIdx * imageWidth + colIdx] = background;

	vec2 p1 = { imageWidth / 2, imageHeight / 4 };
	vec2 p2 = { imageWidth / 4, imageHeight / 4 * 3 };
	vec2 p3 = { imageWidth / 4 * 3, imageHeight / 4 * 3 };
	DrawTriangle(buffer, p1, p2, p3);
	for (int rowIdx = 0; rowIdx < imageHeight; ++rowIdx) {
		for (int colIdx = 0; colIdx < imageWidth; ++colIdx) {
			int idx = rowIdx * imageWidth + colIdx;
			ppmFileStream << (uint32_t)buffer[idx].r << ' ' << (uint32_t)buffer[idx].g << ' ' << (uint32_t)buffer[idx].b << '\t';
		}
		ppmFileStream << "\n";
	}
	delete[] buffer;
	ppmFileStream.close();
}

int main() {
	
	WriteRectangles();
	WriteTriangle();
	return 0;
}
