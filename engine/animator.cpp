#include "animator.h"

#include <algorithm>
#include <cmath>

namespace eng {

void Animator::setModel(const Model* m) {
    model_ = m;
    cur_ = prev_ = State();
    blendT_ = 1.f;
    if (!m) return;
    world.assign(m->nodes.size(), Mat4());
    jointMat.assign(m->skins.size(), {});
    for (size_t i = 0; i < m->skins.size(); i++) jointMat[i].assign(m->skins[i].joints.size(), Mat4());
    restPose(pose_);
    if (!m->clips.empty()) cur_.clip = 0;
    computeMatrices();
}

float Animator::duration() const {
    if (!model_ || cur_.clip < 0) return 0;
    return model_->clips[cur_.clip].duration;
}

float Animator::travelSpeed() const {
    if (!model_ || cur_.clip < 0) return 0;
    return model_->clips[cur_.clip].travel * (paused ? 0.f : speed);
}

void Animator::play(int clip, float blend) {
    if (!model_ || clip < 0 || clip >= (int)model_->clips.size()) return;
    if (clip == cur_.clip && blendT_ >= 1.f) return;
    prev_ = cur_;
    cur_.clip = clip;
    cur_.time = 0;
    blendT_ = (prev_.clip < 0 || blend <= 0) ? 1.f : 0.f;
    blendDur_ = std::max(blend, 1e-4f);
}

void Animator::restPose(Pose& p) const {
    size_t n = model_->nodes.size();
    p.t.resize(n); p.r.resize(n); p.s.resize(n);
    for (size_t i = 0; i < n; i++) {
        p.t[i] = model_->nodes[i].t;
        p.r[i] = model_->nodes[i].r;
        p.s[i] = model_->nodes[i].s;
    }
}

void Animator::sample(const Clip& c, float time, Pose& p) const {
    restPose(p);
    for (const Channel& ch : c.channels) {
        const size_t n = ch.times.size();
        if (n == 0 || ch.node < 0 || ch.node >= (int)p.t.size()) continue;
        size_t i1 = std::upper_bound(ch.times.begin(), ch.times.end(), time) - ch.times.begin();
        size_t i0;
        float f = 0;
        if (i1 == 0) { i0 = i1 = 0; }
        else if (i1 >= n) { i0 = i1 = n - 1; }
        else {
            i0 = i1 - 1;
            float d = ch.times[i1] - ch.times[i0];
            f = d > 1e-9f ? (time - ch.times[i0]) / d : 0.f;
            if (ch.step) f = 0;
        }
        if (ch.path == Path::Rotation) {
            const float* a = &ch.values[i0 * 4];
            const float* b = &ch.values[i1 * 4];
            p.r[ch.node] = (i0 == i1) ? normalize(Quat(a[0], a[1], a[2], a[3]))
                                      : slerp(normalize(Quat(a[0], a[1], a[2], a[3])),
                                              normalize(Quat(b[0], b[1], b[2], b[3])), f);
        } else {
            const float* a = &ch.values[i0 * 3];
            const float* b = &ch.values[i1 * 3];
            Vec3 va(a[0], a[1], a[2]), vb(b[0], b[1], b[2]);
            Vec3 v = (i0 == i1) ? va : lerp(va, vb, f);
            if (ch.path == Path::Translation) p.t[ch.node] = v;
            else p.s[ch.node] = v;
        }
    }
}

void Animator::advance(State& s, float dt) const {
    if (s.clip < 0) return;
    const Clip& c = model_->clips[s.clip];
    s.time += dt;
    if (c.duration > 1e-6f) {
        if (c.loop) s.time = std::fmod(s.time, c.duration);
        else s.time = std::min(s.time, c.duration);
    }
}

void Animator::update(float dt) {
    if (!model_) return;
    if (!paused) {
        float d = dt * speed;
        advance(cur_, d);
        if (blendT_ < 1.f) advance(prev_, d);
        blendT_ = std::min(1.f, blendT_ + dt / blendDur_);
    }
    if (cur_.clip >= 0) {
        sample(model_->clips[cur_.clip], cur_.time, pose_);
        if (blendT_ < 1.f && prev_.clip >= 0) {
            sample(model_->clips[prev_.clip], prev_.time, poseA_);
            float w = blendT_ * blendT_ * (3.f - 2.f * blendT_);
            for (size_t i = 0; i < pose_.t.size(); i++) {
                pose_.t[i] = lerp(poseA_.t[i], pose_.t[i], w);
                pose_.s[i] = lerp(poseA_.s[i], pose_.s[i], w);
                pose_.r[i] = nlerp(poseA_.r[i], pose_.r[i], w);
            }
        }
    }
    computeMatrices();
}

void Animator::evaluate(int clipA, float timeA, int clipB, float timeB, float w) {
    if (!model_) return;
    int nc = (int)model_->clips.size();
    if (clipA < 0 || clipA >= nc) { restPose(pose_); computeMatrices(); return; }
    sample(model_->clips[clipA], timeA, pose_);
    if (clipB >= 0 && clipB < nc && w > 0.f) {
        sample(model_->clips[clipB], timeB, poseA_);
        w = std::min(w, 1.f);
        w = w * w * (3.f - 2.f * w);
        // w - вес клипа B (куда переходим): pose_ = A, poseA_ = B
        for (size_t i = 0; i < pose_.t.size(); i++) {
            pose_.t[i] = lerp(pose_.t[i], poseA_.t[i], w);
            pose_.s[i] = lerp(pose_.s[i], poseA_.s[i], w);
            pose_.r[i] = nlerp(pose_.r[i], poseA_.r[i], w);
        }
    }
    computeMatrices();
}

void Animator::computeMatrices() {
    for (int i : model_->order) {
        Mat4 local = trs(pose_.t[i], pose_.r[i], pose_.s[i]);
        int p = model_->nodes[i].parent;
        world[i] = p >= 0 ? world[p] * local : local;
    }
    for (size_t si = 0; si < model_->skins.size(); si++) {
        const Skin& sk = model_->skins[si];
        for (size_t j = 0; j < sk.joints.size(); j++) jointMat[si][j] = world[sk.joints[j]] * sk.invBind[j];
    }
}

}  // namespace eng
