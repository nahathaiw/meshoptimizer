#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// HOTDOG stores three float32 positions followed by four uint8 color values.
// The static assertion prevents compiler padding from silently changing the
// format that is passed to meshoptimizer.
struct Vertex
{
    float x;
    float y;
    float z;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

static_assert(sizeof(Vertex) == 16, "The HOTDOG vertex layout must be 16 bytes");

struct Mesh
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    size_t source_face_count;
};

static bool hostIsLittleEndian()
{
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1;
}

static uint32_t readLittleU32(std::istream& input, bool& ok)
{
    uint8_t bytes[4] = {};
    input.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (!input)
    {
        ok = false;
        return 0;
    }

    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}

static bool parseCount(const std::string& line, const char* prefix, size_t& result)
{
    if (line.compare(0, std::strlen(prefix), prefix) != 0)
        return false;

    std::istringstream stream(line.substr(std::strlen(prefix)));
    stream >> result;
    return bool(stream) && stream.eof();
}

// This deliberately strict parser supports the binary HOTDOG layout documented
// in README.md. Rejecting unknown layouts is safer than decoding the wrong bytes.
static bool loadHotdogPly(const std::string& path, Mesh& mesh, std::string& error)
{
    if (!hostIsLittleEndian())
    {
        error = "this sample currently requires a little-endian CPU";
        return false;
    }

    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input)
    {
        error = "could not open input: " + path;
        return false;
    }

    std::vector<std::string> header;
    std::string line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        header.push_back(line);
        if (line == "end_header")
            break;
        if (header.size() > 256)
        {
            error = "PLY header is unexpectedly long";
            return false;
        }
    }

    if (header.empty() || header.front() != "ply" || header.back() != "end_header")
    {
        error = "input does not contain a complete PLY header";
        return false;
    }
    if (std::find(header.begin(), header.end(), "format binary_little_endian 1.0") == header.end())
    {
        error = "only binary_little_endian PLY version 1.0 is supported";
        return false;
    }

    size_t vertex_count = 0;
    size_t face_count = 0;
    for (size_t i = 0; i < header.size(); ++i)
    {
        size_t value = 0;
        if (parseCount(header[i], "element vertex ", value))
            vertex_count = value;
        if (parseCount(header[i], "element face ", value))
            face_count = value;
    }

    const char* required_properties[] = {
        "property float x", "property float y", "property float z",
        "property uchar red", "property uchar green", "property uchar blue", "property uchar alpha",
        "property list uchar int vertex_indices",
    };
    size_t property_cursor = 0;
    size_t property_count = 0;
    for (size_t i = 0; i < header.size(); ++i)
    {
        if (header[i].compare(0, 9, "property ") == 0)
            ++property_count;
        if (property_cursor < sizeof(required_properties) / sizeof(required_properties[0]) && header[i] == required_properties[property_cursor])
            ++property_cursor;
    }

    if (vertex_count == 0 || face_count == 0 ||
        property_count != sizeof(required_properties) / sizeof(required_properties[0]) ||
        property_cursor != sizeof(required_properties) / sizeof(required_properties[0]))
    {
        error = "PLY must contain HOTDOG's float32 XYZ, uint8 RGBA, and uchar/int face-list layout";
        return false;
    }
    if (vertex_count > std::numeric_limits<unsigned int>::max())
    {
        error = "vertex count exceeds the uint32 index range";
        return false;
    }

    // Part 1: load the original vertex records without quantization or format
    // conversion. These exact bytes become the vertex codec's input.
    mesh.vertices.resize(vertex_count);
    input.read(reinterpret_cast<char*>(mesh.vertices.data()), std::streamsize(vertex_count * sizeof(Vertex)));
    if (!input)
    {
        error = "input ended while reading vertex records";
        return false;
    }

    // Part 2: read each polygon and triangulate it as a fan. HOTDOG contains
    // triangles, but fan triangulation makes the behavior explicit and safe.
    mesh.source_face_count = face_count;
    bool ok = true;
    for (size_t face = 0; face < face_count; ++face)
    {
        const int count_value = input.get();
        if (count_value < 0)
        {
            error = "input ended while reading face list length";
            return false;
        }

        const size_t count = static_cast<uint8_t>(count_value);
        if (count < 3)
        {
            error = "face " + std::to_string(face) + " has fewer than three vertices";
            return false;
        }

        std::vector<unsigned int> polygon(count);
        for (size_t i = 0; i < count; ++i)
        {
            polygon[i] = readLittleU32(input, ok);
            if (!ok || polygon[i] >= vertex_count)
            {
                error = "face " + std::to_string(face) + " contains invalid index data";
                return false;
            }
        }

        for (size_t i = 2; i < count; ++i)
        {
            mesh.indices.push_back(polygon[0]);
            mesh.indices.push_back(polygon[i - 1]);
            mesh.indices.push_back(polygon[i]);
        }
    }

    if (input.peek() != std::char_traits<char>::eof())
    {
        error = "unexpected bytes remain after the declared PLY elements";
        return false;
    }

    return true;
}

static bool writeBinary(const std::string& path, const void* data, size_t size)
{
    std::ofstream output(path.c_str(), std::ios::binary);
    output.write(reinterpret_cast<const char*>(data), std::streamsize(size));
    return bool(output);
}

static bool readBinary(const std::string& path, std::vector<unsigned char>& data)
{
    std::ifstream input(path.c_str(), std::ios::binary | std::ios::ate);
    if (!input)
        return false;
    const std::streamoff size = input.tellg();
    if (size < 0)
        return false;
    input.seekg(0);
    data.resize(size_t(size));
    input.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
    return bool(input);
}

typedef std::array<unsigned int, 3> Triangle;

static std::vector<Triangle> sortedTriangles(const std::vector<unsigned int>& indices)
{
    std::vector<Triangle> result(indices.size() / 3);
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = {{indices[i * 3 + 0], indices[i * 3 + 1], indices[i * 3 + 2]}};
    std::sort(result.begin(), result.end());
    return result;
}

static const char* passFail(bool value)
{
    return value ? "PASS" : "FAIL";
}

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::cerr << "Usage: " << argv[0] << " INPUT.ply OUTPUT_DIR RESULTS_DIR --optimize|--no-optimize\n";
        return 2;
    }

    const std::string input_path = argv[1];
    const std::string output_dir = argv[2];
    const std::string results_dir = argv[3];
    const std::string mode = argv[4];
    if (mode != "--optimize" && mode != "--no-optimize")
    {
        std::cerr << "Error: invalid mode " << mode << "\n";
        return 2;
    }

    Mesh source;
    std::string error;
    if (!loadHotdogPly(input_path, source, error))
    {
        std::cerr << "PLY loading failed: " << error << "\n";
        return 1;
    }

    // Part 3: optionally reorder complete triangles for GPU vertex-cache reuse.
    // Positions, colors, winding, and triangle membership remain unchanged.
    std::vector<unsigned int> prepared_indices = source.indices;
    if (mode == "--optimize")
        meshopt_optimizeVertexCache(prepared_indices.data(), source.indices.data(), source.indices.size(), source.vertices.size());

    const bool geometry_equal = sortedTriangles(source.indices) == sortedTriangles(prepared_indices);
    const size_t vertex_bytes = source.vertices.size() * sizeof(Vertex);
    const size_t index_bytes = prepared_indices.size() * sizeof(unsigned int);

    // Part 4: losslessly encode every byte in the vertex buffer. No quantizer or
    // meshoptimizer attribute filter is called anywhere in this program.
    std::vector<unsigned char> encoded_vertices(meshopt_encodeVertexBufferBound(source.vertices.size(), sizeof(Vertex)));
    const size_t encoded_vertex_size = meshopt_encodeVertexBuffer(encoded_vertices.data(), encoded_vertices.size(), source.vertices.data(), source.vertices.size(), sizeof(Vertex));
    if (encoded_vertex_size == 0)
    {
        std::cerr << "Vertex encoding failed\n";
        return 1;
    }
    encoded_vertices.resize(encoded_vertex_size);

    // Part 5: use the index-sequence codec so the decoder must reconstruct the
    // exact prepared index order, not merely an equivalent rotated triangle.
    std::vector<unsigned char> encoded_indices(meshopt_encodeIndexSequenceBound(prepared_indices.size(), source.vertices.size()));
    const size_t encoded_index_size = meshopt_encodeIndexSequence(encoded_indices.data(), encoded_indices.size(), prepared_indices.data(), prepared_indices.size());
    if (encoded_index_size == 0)
    {
        std::cerr << "Index encoding failed\n";
        return 1;
    }
    encoded_indices.resize(encoded_index_size);

    const std::string vertex_encoded_path = output_dir + "/hotdog.vertex.meshopt";
    const std::string index_encoded_path = output_dir + "/hotdog.index.meshopt";
    if (!writeBinary(vertex_encoded_path, encoded_vertices.data(), encoded_vertices.size()) ||
        !writeBinary(index_encoded_path, encoded_indices.data(), encoded_indices.size()))
    {
        std::cerr << "Writing encoded streams failed\n";
        return 1;
    }

    // Part 6: read the files back so validation covers actual stored artifacts,
    // rather than decoding only the in-memory encoder output.
    std::vector<unsigned char> stored_vertices;
    std::vector<unsigned char> stored_indices;
    if (!readBinary(vertex_encoded_path, stored_vertices) || !readBinary(index_encoded_path, stored_indices))
    {
        std::cerr << "Reading encoded streams failed\n";
        return 1;
    }

    // Part 7: decode into fresh buffers and always preserve the return codes.
    std::vector<Vertex> decoded_vertices(source.vertices.size());
    std::vector<unsigned int> decoded_indices(prepared_indices.size());
    const int vertex_decode_status = meshopt_decodeVertexBuffer(decoded_vertices.data(), decoded_vertices.size(), sizeof(Vertex), stored_vertices.data(), stored_vertices.size());
    const int index_decode_status = meshopt_decodeIndexSequence(decoded_indices.data(), decoded_indices.size(), sizeof(unsigned int), stored_indices.data(), stored_indices.size());

    const std::string vertex_decoded_path = output_dir + "/hotdog.vertex.decoded.bin";
    const std::string index_decoded_path = output_dir + "/hotdog.index.decoded.bin";
    if (!writeBinary(vertex_decoded_path, decoded_vertices.data(), vertex_bytes) ||
        !writeBinary(index_decoded_path, decoded_indices.data(), index_bytes))
    {
        std::cerr << "Writing decoded buffers failed\n";
        return 1;
    }

    // Part 8: counts are useful, but strict losslessness is proven by comparing
    // every decoded byte with the buffers supplied to the encoders.
    const bool vertex_status_ok = vertex_decode_status == 0;
    const bool index_status_ok = index_decode_status == 0;
    const bool vertex_count_equal = decoded_vertices.size() == source.vertices.size();
    const bool index_count_equal = decoded_indices.size() == prepared_indices.size();
    const bool face_count_equal = decoded_indices.size() / 3 == source.indices.size() / 3;
    const bool vertex_bytes_equal = vertex_status_ok && std::memcmp(source.vertices.data(), decoded_vertices.data(), vertex_bytes) == 0;
    const bool index_bytes_equal = index_status_ok && std::memcmp(prepared_indices.data(), decoded_indices.data(), index_bytes) == 0;
    const bool all_pass = vertex_status_ok && index_status_ok && vertex_count_equal && index_count_equal && face_count_equal && vertex_bytes_equal && index_bytes_equal && geometry_equal;

    const size_t raw_bytes = vertex_bytes + index_bytes;
    const size_t encoded_bytes = stored_vertices.size() + stored_indices.size();
    const double ratio = raw_bytes ? 100.0 * double(encoded_bytes) / double(raw_bytes) : 0.0;
    const double saved = 100.0 - ratio;

    // Keep the terminal output friendly for people running the example. The
    // report below remains key=value text for scripts and automated checks.
    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
    std::cout << "\n============================================================\n"
              << "  Meshoptimizer HOTDOG lossless round trip\n"
              << "============================================================\n\n"
              << "1. INPUT\n"
              << "   File          : " << input_path << "\n"
              << "   Vertices      : " << source.vertices.size() << "\n"
              << "   Faces         : " << source.indices.size() / 3 << "\n"
              << "   Indices       : " << source.indices.size() << "\n"
              << "   Vertex format : float32 XYZ + uint8 RGBA\n"
              << "   Quantization  : NO\n"
              << "   Optimization  : " << (mode == "--optimize" ? "vertex-cache triangle reorder" : "none") << "\n\n"
              << "2. ENCODE\n"
              << "   Stream       Raw bytes       Encoded bytes\n"
              << "   Vertex       " << vertex_bytes << "         " << stored_vertices.size() << "\n"
              << "   Index        " << index_bytes << "        " << stored_indices.size() << "\n"
              << "   Combined     " << raw_bytes << "        " << encoded_bytes << "\n"
              << "   Encoded/raw   : " << ratio << "%\n"
              << "   Space saved   : " << saved << "%\n\n"
              << "3. DECODE\n"
              << "   Vertex stream : " << passFail(vertex_status_ok) << " (return code " << vertex_decode_status << ")\n"
              << "   Index stream  : " << passFail(index_status_ok) << " (return code " << index_decode_status << ")\n\n"
              << "4. VALIDATION\n"
              << "   Vertex count unchanged       : " << passFail(vertex_count_equal) << "\n"
              << "   Face count unchanged         : " << passFail(face_count_equal) << "\n"
              << "   Index count unchanged        : " << passFail(index_count_equal) << "\n"
              << "   Every vertex byte identical  : " << passFail(vertex_bytes_equal) << "\n"
              << "   Every index byte identical   : " << passFail(index_bytes_equal) << "\n"
              << "   Geometry unchanged           : " << passFail(geometry_equal) << "\n\n"
              << "------------------------------------------------------------\n"
              << "  OVERALL STRICT LOSSLESS RESULT: " << passFail(all_pass) << "\n"
              << "------------------------------------------------------------\n";

    std::ostringstream report;
    report.setf(std::ios::fixed);
    report.precision(2);
    report << "Meshoptimizer Lossless HOTDOG Sample\n"
           << "=====================================\n"
           << "input=" << input_path << "\n"
           << "mode=" << mode << "\n"
           << "quantization_used=NO\n"
           << "source_vertex_count=" << source.vertices.size() << "\n"
           << "source_face_records=" << source.source_face_count << "\n"
           << "triangulated_face_count=" << source.indices.size() / 3 << "\n"
           << "source_index_count=" << source.indices.size() << "\n"
           << "raw_vertex_bytes=" << vertex_bytes << "\n"
           << "encoded_vertex_bytes=" << stored_vertices.size() << "\n"
           << "raw_index_bytes=" << index_bytes << "\n"
           << "encoded_index_bytes=" << stored_indices.size() << "\n"
           << "combined_raw_bytes=" << raw_bytes << "\n"
           << "combined_encoded_bytes=" << encoded_bytes << "\n"
           << "encoded_raw_percent=" << ratio << "\n"
           << "vertex_decode_status=" << vertex_decode_status << " " << passFail(vertex_status_ok) << "\n"
           << "index_decode_status=" << index_decode_status << " " << passFail(index_status_ok) << "\n"
           << "vertex_count_equal=" << passFail(vertex_count_equal) << "\n"
           << "face_count_equal=" << passFail(face_count_equal) << "\n"
           << "index_count_equal=" << passFail(index_count_equal) << "\n"
           << "vertex_bytes_equal=" << passFail(vertex_bytes_equal) << "\n"
           << "index_bytes_equal=" << passFail(index_bytes_equal) << "\n"
           << "geometry_equal_before_after_optimization=" << passFail(geometry_equal) << "\n"
           << "overall_strict_lossless=" << passFail(all_pass) << "\n";

    const std::string report_text = report.str();
    if (!writeBinary(results_dir + "/report.txt", report_text.data(), report_text.size()))
    {
        std::cerr << "Writing report failed\n";
        return 1;
    }

    std::ostringstream validation;
    validation << "vertex_decode=" << passFail(vertex_status_ok) << "\n"
               << "index_decode=" << passFail(index_status_ok) << "\n"
               << "vertex_count=" << passFail(vertex_count_equal) << "\n"
               << "face_count=" << passFail(face_count_equal) << "\n"
               << "index_count=" << passFail(index_count_equal) << "\n"
               << "vertex_bytes=" << passFail(vertex_bytes_equal) << "\n"
               << "index_bytes=" << passFail(index_bytes_equal) << "\n"
               << "geometry=" << passFail(geometry_equal) << "\n"
               << "overall=" << passFail(all_pass) << "\n";
    const std::string validation_text = validation.str();
    if (!writeBinary(results_dir + "/validation.txt", validation_text.data(), validation_text.size()))
    {
        std::cerr << "Writing validation failed\n";
        return 1;
    }

    return all_pass ? 0 : 1;
}
