/*
* Copyright 2013 Google Inc.
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#include "GrPathProcessor.h"

#include "gl/GrGLGpu.h"
#include "glsl/GrGLSLCaps.h"
#include "glsl/GrGLSLFragmentShaderBuilder.h"
#include "glsl/GrGLSLProcessorTypes.h"
#include "glsl/GrGLSLProgramBuilder.h"
#include "glsl/GrGLSLVarying.h"

class GrGLPathProcessor : public GrGLSLPrimitiveProcessor {
public:
    GrGLPathProcessor() : fColor(GrColor_ILLEGAL) {}

    static void GenKey(const GrPathProcessor& pathProc,
                       const GrGLSLCaps&,
                       GrProcessorKeyBuilder* b) {
        b->add32(SkToInt(pathProc.opts().readsColor()) |
                 SkToInt(pathProc.opts().readsCoverage()) << 16);
    }

    void emitCode(EmitArgs& args) override {
        GrGLSLGPBuilder* pb = args.fPB;
        GrGLSLFragmentBuilder* fragBuilder = args.fFragBuilder;
        const GrPathProcessor& pathProc = args.fGP.cast<GrPathProcessor>();

        // emit transforms
        this->emitTransforms(args.fVaryingHandler, args.fTransformsIn, args.fTransformsOut);

        // Setup uniform color
        if (pathProc.opts().readsColor()) {
            const char* stagedLocalVarName;
            fColorUniform = pb->addUniform(GrGLSLProgramBuilder::kFragment_Visibility,
                                           kVec4f_GrSLType,
                                           kDefault_GrSLPrecision,
                                           "Color",
                                           &stagedLocalVarName);
            fragBuilder->codeAppendf("%s = %s;", args.fOutputColor, stagedLocalVarName);
        }

        // setup constant solid coverage
        if (pathProc.opts().readsCoverage()) {
            fragBuilder->codeAppendf("%s = vec4(1);", args.fOutputCoverage);
        }
    }

    void emitTransforms(GrGLSLVaryingHandler* varyingHandler,
                        const TransformsIn& tin,
                        TransformsOut* tout) {
        tout->push_back_n(tin.count());
        fInstalledTransforms.push_back_n(tin.count());
        for (int i = 0; i < tin.count(); i++) {
            const ProcCoords& coordTransforms = tin[i];
            fInstalledTransforms[i].push_back_n(coordTransforms.count());
            for (int t = 0; t < coordTransforms.count(); t++) {
                GrSLType varyingType =
                        coordTransforms[t]->getMatrix().hasPerspective() ? kVec3f_GrSLType :
                                                                           kVec2f_GrSLType;

                SkString strVaryingName("MatrixCoord");
                strVaryingName.appendf("_%i_%i", i, t);
                GrGLSLVertToFrag v(varyingType);
                GrGLVaryingHandler* glVaryingHandler = (GrGLVaryingHandler*) varyingHandler;
                fInstalledTransforms[i][t].fHandle =
                        glVaryingHandler->addPathProcessingVarying(strVaryingName.c_str(),
                                                                   &v).toIndex();
                fInstalledTransforms[i][t].fType = varyingType;

                SkNEW_APPEND_TO_TARRAY(&(*tout)[i], GrGLSLTransformedCoords,
                                       (SkString(v.fsIn()), varyingType));
            }
        }
    }

    void setData(const GrGLSLProgramDataManager& pd,
                 const GrPrimitiveProcessor& primProc) override {
        const GrPathProcessor& pathProc = primProc.cast<GrPathProcessor>();
        if (pathProc.opts().readsColor() && pathProc.color() != fColor) {
            float c[4];
            GrColorToRGBAFloat(pathProc.color(), c);
            pd.set4fv(fColorUniform, 1, c);
            fColor = pathProc.color();
        }
    }

    void setTransformData(const GrPrimitiveProcessor& primProc,
                          const GrGLSLProgramDataManager& pdman,
                          int index,
                          const SkTArray<const GrCoordTransform*, true>& coordTransforms) override {
        const GrPathProcessor& pathProc = primProc.cast<GrPathProcessor>();
        SkSTArray<2, Transform, true>& transforms = fInstalledTransforms[index];
        int numTransforms = transforms.count();
        for (int t = 0; t < numTransforms; ++t) {
            SkASSERT(transforms[t].fHandle.isValid());
            const SkMatrix& transform = GetTransformMatrix(pathProc.localMatrix(),
                                                           *coordTransforms[t]);
            if (transforms[t].fCurrentValue.cheapEqualTo(transform)) {
                continue;
            }
            transforms[t].fCurrentValue = transform;

            SkASSERT(transforms[t].fType == kVec2f_GrSLType ||
                     transforms[t].fType == kVec3f_GrSLType);
            unsigned components = transforms[t].fType == kVec2f_GrSLType ? 2 : 3;
            pdman.setPathFragmentInputTransform(transforms[t].fHandle, components, transform);
        }
    }

private:
    UniformHandle fColorUniform;
    GrColor fColor;

    typedef GrGLSLPrimitiveProcessor INHERITED;
};

GrPathProcessor::GrPathProcessor(GrColor color,
                                 const GrPipelineOptimizations& opts,
                                 const SkMatrix& viewMatrix,
                                 const SkMatrix& localMatrix)
    : INHERITED(true)
    , fColor(color)
    , fViewMatrix(viewMatrix)
    , fLocalMatrix(localMatrix)
    , fOpts(opts) {
    this->initClassID<GrPathProcessor>();
}

void GrPathProcessor::getGLSLProcessorKey(const GrGLSLCaps& caps,
                                          GrProcessorKeyBuilder* b) const {
    GrGLPathProcessor::GenKey(*this, caps, b);
}

GrGLSLPrimitiveProcessor* GrPathProcessor::createGLSLInstance(const GrGLSLCaps& caps) const {
    SkASSERT(caps.pathRenderingSupport());
    return new GrGLPathProcessor();
}
