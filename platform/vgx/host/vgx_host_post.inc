// The window's present as shader passes (included by vgx_host_sdl.cpp): the picture from the EFB's colour texture to the window's
// view, with FXAA (at the render size) and AMD FidelityFX Super Resolution 1 (EASU, the edge-adaptive upscale to the view's size,
// then RCAS, its contrast-adaptive sharpening) when the PC Settings ask for them. Everything is OpenGL ES 2.0 (GLSL ES 1.00), so it
// runs on ANGLE's Direct3D 11 and Direct3D 9 renderers alike. A pass whose program does not build is left out (logged once):
// the plain bilinear upscale is what remains.
//
// Passes (each a full-viewport quad; uSrc maps the target's top row onward to the source rectangle, see kPostVs):
//   FXAA   EFB rectangle -> g_rtA (render size)              FXAA 3.11-style "console" filter: 4 diagonal taps find the edge,
//                                                             2 or 4 taps along it; flat pixels pass unchanged.
//   copy   EFB rectangle -> g_rtA (render size)              only when FSR follows without FXAA (EASU reads a whole texture).
//   EASU   g_rtA -> g_rtB (view size)                        FSR 1 EASU, 12 taps, direction- and length-adaptive Lanczos-like lobe,
//                                                             clamped to the 2x2 neighbourhood (no ringing).
//   RCAS   g_rtB -> window view                              FSR 1 RCAS at 0.2 stops (sharpness 0.87), noise removal off.
//   blit   (last source) -> window view                      bilinear: no FSR, or FSR asked while the view is not larger.

namespace post {

const char* const kPostVs =
    "attribute vec2 aPos;\n"
    "uniform vec4 uSrc;\n"
    "varying vec2 vUv;\n"
    "void main() {\n"
    "    vec2 t = aPos * 0.5 + 0.5;\n"
    "    vUv = uSrc.xy + vec2(t.x, 1.0 - t.y) * uSrc.zw;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

#define POST_FS_HEAD                                                                                                                 \
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\n"                        \
    "uniform sampler2D uTex;\n"                                                                                                      \
    "varying vec2 vUv;\n"

const char* const kCopyFs = POST_FS_HEAD "void main() { gl_FragColor = vec4(texture2D(uTex, vUv).rgb, 1.0); }\n";

const char* const kFxaaFs = POST_FS_HEAD
    "uniform vec2 uInv;\n"
    "float L(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }\n"
    "void main() {\n"
    "    vec3 rgbM = texture2D(uTex, vUv).rgb;\n"
    "    vec3 rgbNW = texture2D(uTex, vUv + vec2(-1.0, -1.0) * uInv).rgb;\n"
    "    vec3 rgbNE = texture2D(uTex, vUv + vec2(1.0, -1.0) * uInv).rgb;\n"
    "    vec3 rgbSW = texture2D(uTex, vUv + vec2(-1.0, 1.0) * uInv).rgb;\n"
    "    vec3 rgbSE = texture2D(uTex, vUv + vec2(1.0, 1.0) * uInv).rgb;\n"
    "    float lM = L(rgbM), lNW = L(rgbNW), lNE = L(rgbNE), lSW = L(rgbSW), lSE = L(rgbSE);\n"
    "    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
    "    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
    "    if (lMax - lMin < max(0.0312, lMax * 0.125)) { gl_FragColor = vec4(rgbM, 1.0); return; }\n"
    "    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));\n"
    "    float red = max((lNW + lNE + lSW + lSE) * (0.25 / 8.0), 1.0 / 128.0);\n"
    "    float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);\n"
    "    dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * uInv;\n"
    "    vec3 rgbA = 0.5 * (texture2D(uTex, vUv + dir * (1.0 / 3.0 - 0.5)).rgb + texture2D(uTex, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
    "    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture2D(uTex, vUv - dir * 0.5).rgb + texture2D(uTex, vUv + dir * 0.5).rgb);\n"
    "    float lB = L(rgbB);\n"
    "    gl_FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);\n"
    "}\n";

// FSR 1 EASU (AMD FidelityFX, MIT licence), written out for GLSL ES 1.00 without gathers: the 12 taps around the output pixel's
// position in the input,
//       b c
//     e f g h
//     i j k l
//       n o
// with f the texel at floor(pp). uCon.xy: the input's size, uCon.zw: the output's (the pass draws the whole output at origin 0).
const char* const kEasuFs = POST_FS_HEAD
    "uniform vec4 uCon;\n"
    "vec3 T(vec2 fp, vec2 o) { return texture2D(uTex, (fp + o + 0.5) / uCon.xy).rgb; }\n"
    "float Lu(vec3 c) { return c.g + 0.5 * (c.r + c.b); }\n"
    "void Set(inout vec2 dir, inout float len, float w, float lA, float lB, float lC, float lD, float lE) {\n"
    "    float lenX = max(abs(lD - lC), abs(lC - lB));\n"
    "    float dirX = lD - lB;\n"
    "    dir.x += dirX * w;\n"
    "    lenX = clamp(abs(dirX) / max(lenX, 1.0 / 65536.0), 0.0, 1.0);\n"
    "    lenX *= lenX;\n"
    "    len += lenX * w;\n"
    "    float lenY = max(abs(lE - lC), abs(lC - lA));\n"
    "    float dirY = lE - lA;\n"
    "    dir.y += dirY * w;\n"
    "    lenY = clamp(abs(dirY) / max(lenY, 1.0 / 65536.0), 0.0, 1.0);\n"
    "    lenY *= lenY;\n"
    "    len += lenY * w;\n"
    "}\n"
    "void Tap(inout vec3 aC, inout float aW, vec2 off, vec2 dir, vec2 len2, float lob, float clp, vec3 c) {\n"
    "    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.x * (-dir.y) + off.y * dir.x) * len2;\n"
    "    float d2 = min(v.x * v.x + v.y * v.y, clp);\n"
    "    float wB = (2.0 / 5.0) * d2 - 1.0;\n"
    "    float wA = lob * d2 - 1.0;\n"
    "    wB *= wB;\n"
    "    wA *= wA;\n"
    "    wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0);\n"
    "    float w = wB * wA;\n"
    "    aC += c * w;\n"
    "    aW += w;\n"
    "}\n"
    "void main() {\n"
    "    vec2 pp = gl_FragCoord.xy * (uCon.xy / uCon.zw) - 0.5;\n"
    "    vec2 fp = floor(pp);\n"
    "    pp -= fp;\n"
    "    vec3 b = T(fp, vec2(0.0, -1.0)), c = T(fp, vec2(1.0, -1.0));\n"
    "    vec3 e = T(fp, vec2(-1.0, 0.0)), f = T(fp, vec2(0.0, 0.0)), g = T(fp, vec2(1.0, 0.0)), h = T(fp, vec2(2.0, 0.0));\n"
    "    vec3 i = T(fp, vec2(-1.0, 1.0)), j = T(fp, vec2(0.0, 1.0)), k = T(fp, vec2(1.0, 1.0)), l = T(fp, vec2(2.0, 1.0));\n"
    "    vec3 n = T(fp, vec2(0.0, 2.0)), o = T(fp, vec2(1.0, 2.0));\n"
    "    float bL = Lu(b), cL = Lu(c), eL = Lu(e), fL = Lu(f), gL = Lu(g), hL = Lu(h);\n"
    "    float iL = Lu(i), jL = Lu(j), kL = Lu(k), lL = Lu(l), nL = Lu(n), oL = Lu(o);\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    Set(dir, len, (1.0 - pp.x) * (1.0 - pp.y), bL, eL, fL, gL, jL);\n"
    "    Set(dir, len, pp.x * (1.0 - pp.y), cL, fL, gL, hL, kL);\n"
    "    Set(dir, len, (1.0 - pp.x) * pp.y, fL, iL, jL, kL, nL);\n"
    "    Set(dir, len, pp.x * pp.y, gL, jL, kL, lL, oL);\n"
    "    vec2 dir2 = dir * dir;\n"
    "    float dirR = dir2.x + dir2.y;\n"
    "    bool zro = dirR < 1.0 / 32768.0;\n"
    "    dirR = zro ? 1.0 : inversesqrt(dirR);\n"
    "    dir.x = zro ? 1.0 : dir.x;\n"
    "    dir *= dirR;\n"
    "    len = len * 0.5;\n"
    "    len *= len;\n"
    "    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;\n"
    "    float clp = 1.0 / lob;\n"
    "    vec3 mn = min(min(f, g), min(j, k)), mx = max(max(f, g), max(j, k));\n"
    "    vec3 aC = vec3(0.0);\n"
    "    float aW = 0.0;\n"
    "    Tap(aC, aW, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "    Tap(aC, aW, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "    Tap(aC, aW, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);\n"
    "    Tap(aC, aW, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "    Tap(aC, aW, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "    Tap(aC, aW, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "    Tap(aC, aW, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "    Tap(aC, aW, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "    Tap(aC, aW, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "    Tap(aC, aW, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "    Tap(aC, aW, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);\n"
    "    Tap(aC, aW, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "    gl_FragColor = vec4(min(mx, max(mn, aC / aW)), 1.0);\n"
    "}\n";

// FSR 1 RCAS: uInv the source's texel size, uOrigin the view's corner in the window (gl_FragCoord), uSharp exp2(-stops).
const char* const kRcasFs = POST_FS_HEAD
    "uniform vec2 uInv;\n"
    "uniform vec2 uOrigin;\n"
    "uniform float uSharp;\n"
    "void main() {\n"
    "    vec2 uv = (gl_FragCoord.xy - uOrigin) * uInv;\n"
    "    vec3 b = texture2D(uTex, uv + vec2(0.0, uInv.y)).rgb;\n"
    "    vec3 d = texture2D(uTex, uv - vec2(uInv.x, 0.0)).rgb;\n"
    "    vec3 e = texture2D(uTex, uv).rgb;\n"
    "    vec3 f = texture2D(uTex, uv + vec2(uInv.x, 0.0)).rgb;\n"
    "    vec3 h = texture2D(uTex, uv - vec2(0.0, uInv.y)).rgb;\n"
    "    vec3 mn4 = min(min(b, d), min(f, h));\n"
    "    vec3 mx4 = max(max(b, d), max(f, h));\n"
    "    vec3 hitMin = min(mn4, e) / max(4.0 * mx4, vec3(1.0 / 65536.0));\n"
    "    vec3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * min(mn4, e) - 4.0, vec3(-1.0 / 65536.0));\n"
    "    vec3 lobeRGB = max(-hitMin, hitMax);\n"
    "    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * uSharp;\n"
    "    vec3 c = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);\n"
    "    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);\n"
    "}\n";
#undef POST_FS_HEAD

struct Prog {
    GLuint id = 0;
    bool tried = false;
    GLint src = -1, tex = -1, inv = -1, con = -1, origin = -1, sharp = -1;
};
Prog g_copy, g_fxaa, g_easu, g_rcas;

bool Build(Prog& p, const char* fs, const char* name) {
    if (p.tried) return p.id != 0;
    p.tried = true;
    GLuint vs = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
    GLint ok = 0;
    glShaderSource(vs, 1, &kPostVs, nullptr);
    glCompileShader(vs);
    glShaderSource(f, 1, &fs, nullptr);
    glCompileShader(f);
    GLuint id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, f);
    glBindAttribLocation(id, 0, "aPos");
    glLinkProgram(id);
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(f);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(id, sizeof log, nullptr, log);
        if (!log[0]) glGetShaderInfoLog(f, sizeof log, nullptr, log);
        std::fprintf(stderr, "[vgx-sdl] %s pass not available (the program did not build): %s\n", name, log);
        glDeleteProgram(id);
        return false;
    }
    p.id = id;
    p.src = glGetUniformLocation(id, "uSrc");
    p.tex = glGetUniformLocation(id, "uTex");
    p.inv = glGetUniformLocation(id, "uInv");
    p.con = glGetUniformLocation(id, "uCon");
    p.origin = glGetUniformLocation(id, "uOrigin");
    p.sharp = glGetUniformLocation(id, "uSharp");
    std::fprintf(stderr, "[vgx-sdl] %s pass ready\n", name);
    return true;
}

struct Rt {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    bool bad = false;
};
Rt g_rtA, g_rtB;

bool Ensure(Rt& rt, int w, int h) {
    if (rt.bad) return false;
    if (rt.fbo && rt.w == w && rt.h == h) return true;
    if (!rt.tex) glGenTextures(1, &rt.tex);
    if (!rt.fbo) glGenFramebuffers(1, &rt.fbo);
    glBindTexture(GL_TEXTURE_2D, rt.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.tex, 0);
    const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[vgx-sdl] post-processing target %dx%d incomplete (0x%X): off\n", w, h, st);
        rt.bad = true;
        return false;
    }
    rt.w = w;
    rt.h = h;
    return true;
}

// One pass: `p` (already in use, its own uniforms set) reads `tex` through uSrc into `fbo`'s rectangle.
void Quad(const Prog& p, GLuint tex, float s0, float s1, float s2, float s3, GLuint fbo, int vx, int vy, int vw, int vh) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(vx, vy, vw, vh);
    glUniform4f(p.src, s0, s1, s2, s3);
    glUniform1i(p.tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// The picture: EFB colour texture `efbTex` (tw x th), its rectangle x, y, w, h (flipped: row y is the top), into the window's view
// (vx, vy: its bottom-left corner in GL window coordinates; vw x vh). Returns false when not even the copy program is there.
bool Present(GLuint efbTex, int tw, int th, int x, int y, int w, int h, bool flip, int vx, int vy, int vw, int vh, bool fxaa, bool fsr) {
    if (!Build(g_copy, kCopyFs, "copy")) return false;
    if (fxaa) fxaa = Build(g_fxaa, kFxaaFs, "FXAA");
    const bool upscale = vw > w || vh > h;
    if (fsr) fsr = upscale && Build(g_easu, kEasuFs, "FSR EASU") && Build(g_rcas, kRcasFs, "FSR RCAS");
    if ((fxaa || fsr) && !Ensure(g_rtA, w, h)) fxaa = fsr = false;
    if (fsr && !Ensure(g_rtB, vw, vh)) fsr = false;

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    GLint attribOn = 0;
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribOn);
    static const float kQuad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, kQuad);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, efbTex);
    GLint minF = 0, magF = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minF);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // The EFB rectangle as uSrc (the target's top row first).
    float s0 = float(x) / tw, s2 = float(w) / tw;
    float s1 = flip ? float(y) / th : float(y + h) / th, s3 = (flip ? float(h) : -float(h)) / th;
    GLuint cur = efbTex;
    int curW = w, curH = h;
    if (fxaa || fsr) {
        const Prog& p = fxaa ? g_fxaa : g_copy;
        glUseProgram(p.id);
        if (fxaa) glUniform2f(p.inv, 1.0f / tw, 1.0f / th);
        Quad(p, efbTex, s0, s1, s2, s3, g_rtA.fbo, 0, 0, w, h);
        cur = g_rtA.tex;
        s0 = 0.0f; s1 = 1.0f; s2 = 1.0f; s3 = -1.0f;  // (a target of ours: its top row is its last)
    }
    if (fsr) {
        glUseProgram(g_easu.id);
        glUniform4f(g_easu.con, float(curW), float(curH), float(vw), float(vh));
        Quad(g_easu, cur, s0, s1, s2, s3, g_rtB.fbo, 0, 0, vw, vh);
        glUseProgram(g_rcas.id);
        glUniform2f(g_rcas.inv, 1.0f / vw, 1.0f / vh);
        glUniform2f(g_rcas.origin, float(vx), float(vy));
        glUniform1f(g_rcas.sharp, 0.87055f);  // exp2(-0.2)
        Quad(g_rcas, g_rtB.tex, 0.0f, 1.0f, 1.0f, -1.0f, 0, vx, vy, vw, vh);
    } else {
        glUseProgram(g_copy.id);
        Quad(g_copy, cur, s0, s1, s2, s3, 0, vx, vy, vw, vh);
    }

    glBindTexture(GL_TEXTURE_2D, efbTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
    if (!attribOn) glDisableVertexAttribArray(0);
    return true;
}

}  // namespace post
