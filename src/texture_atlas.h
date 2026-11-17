#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

#include <cassert>
#include <iostream>
#include <algorithm>
#include <limits>
#include <vector>
#include <utility>
#include <stdexcept>
#include <string>
#include <cstddef>

#include "vector_util.h"
#include <SFML/Graphics.hpp>

// RectPacker maintains two offsets into an overall bin size:
//  - A line y-offset, representing the current horizontal line on which to place new rectangles
//      horizontally along (the line corresponding to the rectangle tops)
//  - An x-offset into the line, representing the x-coordinate to align the next rectangle left-side
// Furthermore it maintains the maximum texture height encountered along the current line.
// The RectPacker places rectangles left to right along this line until it cannot fit the next
//  rectangle. Once this happens, the line is shifted down to meet the bottom of the tallest texture
//  placed on the line, and the x-offset is reset. This continues until the next rectangle
//  packed cannot be fit on the line within the bin space.
class RectPacker {
public:
    RectPacker(const sf::Vector2f& size) : mSize{size} {};

    // pack returns { succeeded, rect id }. If not succeeded, id is irrelevant (but ideally should
    // be set to the maximum possible value).
    std::pair<bool, size_t> pack(const sf::Vector2f& rectSize) { 
        // If rect size larger than the bin width/height, cannot store it
        if (rectSize.x > mSize.x || rectSize.y > mSize.y) {
            return {false, std::numeric_limits<size_t>::max()};
        }
        // If doesnt fir horizontally, move to the next row
        if (mX + rectSize.x > mSize.x) {
            mY += mMaxHeight; // Move down to the bottom of the tallest rectangle in the prev row
            mX = 0; // start from the left side
            mMaxHeight = 0; // new row so no rect yet
        }
        // Check if it fits vertically
        if (mY + rectSize.y > mSize.y) {
            return {false, std::numeric_limits<size_t>::max()};
        }
        // Place the successfully added rect
        sf::Vector2f position{mX, mY};
        mBounds.push_back(sf::FloatRect{position, rectSize});
        // Update offsets
        mX += rectSize.x; // next to the prev one
        mMaxHeight = std::max(mMaxHeight, rectSize.y); // max between the currently added and the stored max one
        // Return success and the rectangle ID
        return {true, mBounds.size() - 1};
    }

    // Returns the coordinates and size of a previously returned rectangle id.
    // Note that in this coordinate system, y points downwards from the origin at the top-left.
    sf::FloatRect getBounds(size_t rectID) const { 
        assert(rectID < mBounds.size());
        return mBounds[rectID];
    }

private:
    sf::Vector2f mSize; // Total size of the atlas
    float mX = 0; // Horizontal position where the next rectangle starts
    float mY = 0; // Vertical position of the current row
    float mMaxHeight = 0; // Height of the tallest rectangle in the current row
    std::vector<sf::FloatRect> mBounds; // Stores the positions and sizes of all successfully packed rectangles
};

// Returns a new image which pages the given img with *pixels* pixels on each side.
// The colour of new border pixels are taken by sampling the closest valid pixel in the original
// image.
//
// NOTE: a production implementation might also support other edge wrapping behaviors, like cycling
// around to the other side of the texture. See https://learnopengl.com/Getting-started/Textures
// Our implementation corresponds to GL_CLAMP_TO_EDGE
inline sf::Image padImage(const sf::Image& img, uint pixels) { 
    sf::Vector2u origSize = img.getSize();
    // Create a larger image: x2 since we include both sides
    sf::Vector2u newSize{origSize.x + 2 * pixels, origSize.y + 2 * pixels};
    sf::Image padded(newSize);
    // Visit every pixel in the new image
    for (unsigned int y = 0; y < newSize.y; ++y) {
        for (unsigned int x = 0; x < newSize.x; ++x) {
            // Find the closest valid pixel in the original image
            // clamp betwen 0 or the og width - 1
            int srcX = std::clamp(static_cast<int>(x) - static_cast<int>(pixels), 0, static_cast<int>(origSize.x) - 1);

            int srcY = std::clamp(static_cast<int>(y) - static_cast<int>(pixels), 0, static_cast<int>(origSize.y) - 1);

            // Copy its colour into the new image
            padded.setPixel({x, y}, img.getPixel({static_cast<unsigned int>(srcX), static_cast<unsigned int>(srcY)}));
        }
    }
    return padded; 
};

using TextureID = size_t;

class TextureAtlas {
public:
    TextureAtlas(sf::Vector2u binSize, size_t padding)
        : mTexture(binSize), mPacker(sf::Vector2f(binSize)), mPadding{padding} {}

    TextureID registerTexture(const std::string& filepath) { 
        // 1. Load the original image
        sf::Image original;
        if (!original.loadFromFile(filepath)) {
            throw std::runtime_error("Failed to load image: " + filepath);
        }
        // Remember the original size BEFORE padding
        sf::Vector2u originalSize = original.getSize();
         // 2. Add padding around the image
        sf::Image padded = padImage(original, mPadding);
        // 3. Pack the padded image into the atlas
        sf::Vector2u paddedSize = padded.getSize();
        auto [success, id] = mPacker.pack(sf::Vector2f(paddedSize));
        if (!success) {
            throw std::runtime_error("Texture atlas is full: " + filepath);
        }
        // 4. Get the position assigned by RectPacker
        sf::FloatRect bounds = mPacker.getBounds(id);

        // 5. Copy the padded image into the atlas texture
        sf::Vector2u destination{static_cast<unsigned int>(bounds.position.x), static_cast<unsigned int>(bounds.position.y)};
        mTexture.update(padded, destination);

        // 6. Store the original (unpadded) size using the texture ID
        mOriginalSizes.push_back(originalSize);

        return id; 
    }

    sf::FloatRect getBoundsUV(TextureID id) const { 
        assert(id < mOriginalSizes.size());
        // Get the padded rectangle's position
        sf::FloatRect bounds = mPacker.getBounds(id);
        // Get the original image size (without padding)
        sf::Vector2u originalSize = mOriginalSizes[id];
        // Get the atlas dimensions
        sf::Vector2u atlasSize = mTexture.getSize();
        // Find where the original image starts inside the padded rectangle
        float x = bounds.position.x + mPadding; // immage start after the padding
        float y = bounds.position.y + mPadding;

        float width = static_cast<float>(originalSize.x);
        float height = static_cast<float>(originalSize.y);
        // Convert pixel coordinates to normalized OpenGL UV coordinates
        float u = x / atlasSize.x;
        // The top of the original image is at y so bottom is at y + height
        float v = 1.0f - (y + height) / atlasSize.y;

        float uvWidth = width / atlasSize.x;
        float uvHeight = height / atlasSize.y;
        return sf::FloatRect{{u, v}, {uvWidth, uvHeight}};
    }

    sf::Vector2u getSizePixels(TextureID id) const { 
        assert(id < mOriginalSizes.size());
        return mOriginalSizes[id];
    }

    const sf::Texture& getTexture() const { return mTexture; }

private:
    sf::Texture mTexture;
    RectPacker mPacker;
    size_t mPadding;
    std::vector<sf::Vector2u> mOriginalSizes; // RectPacker only knows the bounds
};

#endif  // TEXTURE_ATLAS_H
