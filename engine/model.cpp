#include "model.h"

#include <cstddef>
#include <functional>

namespace eng {

void MeshData::upload() {
    if (vao || verts.empty()) return;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(indices.size() * sizeof(uint32_t)), indices.data(),
                 GL_STATIC_DRAW);
    const GLsizei stride = sizeof(Vertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, p));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, n));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void*)offsetof(Vertex, c));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 2, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_BYTE, stride, (const void*)offsetof(Vertex, j));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(Vertex, w));
    glBindVertexArray(0);
}

void MeshData::updateVerts() {
    if (!vao || !vbo || verts.empty()) { stale = false; return; }
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(Vertex)), verts.data(), GL_DYNAMIC_DRAW);
    stale = false;
}

void MeshData::release() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    vao = vbo = ibo = 0;
}

void MeshData::draw() const {
    if (!vao) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
}

void Model::finalize() {
    // Топологический порядок (родители раньше детей).
    order.clear();
    std::vector<char> done(nodes.size(), 0);
    std::function<void(int, int)> visit = [&](int i, int depth) {
        if (done[i] || depth > (int)nodes.size()) return;
        int p = nodes[i].parent;
        if (p >= 0 && p < (int)nodes.size()) visit(p, depth + 1);
        else nodes[i].parent = -1;
        if (!done[i]) { done[i] = 1; order.push_back(i); }
    };
    for (int i = 0; i < (int)nodes.size(); i++) visit(i, 0);

    // Мировые матрицы в bind-позе.
    std::vector<Mat4> world(nodes.size());
    for (int i : order) {
        Mat4 local = trs(nodes[i].t, nodes[i].r, nodes[i].s);
        world[i] = nodes[i].parent >= 0 ? world[nodes[i].parent] * local : local;
    }
    // Границы.
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (int i : order) {
        for (int mi : nodes[i].meshes) {
            const MeshData& m = meshes[mi];
            bool skinned = m.skin >= 0;
            for (const Vertex& v : m.verts) {
                Vec3 p{v.p[0], v.p[1], v.p[2]};
                if (!skinned) p = transformPoint(world[i], p);
                lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                any = true;
            }
        }
    }
    if (any) { bmin = lo; bmax = hi; }
}

void Texture::upload() {
    if (id || rgba.empty()) return;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}
void Texture::release() {
    if (id) glDeleteTextures(1, &id);
    id = 0;
}

void Model::upload() {
    for (auto& t : textures) t.upload();
    for (auto& m : meshes) m.upload();
    uploaded = true;
}
void Model::release() {
    for (auto& t : textures) t.release();
    for (auto& m : meshes) m.release();
    uploaded = false;
}
size_t Model::triangles() const {
    size_t n = 0;
    for (auto& m : meshes) n += m.triangles();
    return n;
}
size_t Model::jointCount() const {
    size_t n = 0;
    for (auto& s : skins) n += s.joints.size();
    return n;
}

}  // namespace eng
