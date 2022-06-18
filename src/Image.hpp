#pragma once

#include "Utils.hpp"

#include <fstream>
#include <vector>

struct ImageData;

/// Data for a png image with 3 component color, used to write render results
struct PNGImage {
#pragma pack(push, 1)
	union Pixel {
		uint8_t rgb[3];
		struct {
			uint8_t r, g, b;
		};
	};
#pragma pack(pop)

	static int componentCount() {
		return int(sizeof(Pixel::rgb) / sizeof(Pixel::rgb[0]));
	}

	std::vector<Pixel> data;

	PNGImage(int w, int h)
		: data(w * h)
	{}
};

typedef vec3 Color;

/// Image used while rendering with 32 bit float for each color component
struct ImageData {
	int width = 0;
	int height = 0;
	std::vector<Color> pixels;

	void init(int w, int h) {
		width = w;
		height = h;
		pixels.resize(width * height);
	}

	ImageData(int width, int height) {
		init(width, height);
	}

	ImageData() = default;

	PNGImage createPNGData() const {
		PNGImage img(width, height);
		for (int c = 0; c < pixels.size(); c++) {
			PNGImage::Pixel &out = img.data[c];
			const Color &in = pixels[c];
			float sum = 0.f;
			for (int r = 0; r < 3; r++) {
				out.rgb[r] = in[r] * 255.0;
				sum += in[r];
			}
		}
		return img;
	}

	// wtf is this, who does this
	Color &operator()(int col, int row) {
		return pixels[row * width + col];
	}

	const Color &operator()(int col, int row) const {
		return pixels[row * width + col];
	}
};
