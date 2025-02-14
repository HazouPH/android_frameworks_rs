/*
 * Copyright (C) 2009-2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_RS_SCRIPT_H
#define ANDROID_RS_SCRIPT_H

#include "rsAllocation.h"

#include <utility>

// ---------------------------------------------------------------------------
namespace android {
namespace renderscript {

#ifndef RS_COMPATIBILITY_LIB
class ProgramVertex;
class ProgramFragment;
class ProgramRaster;
class ProgramStore;
#endif

class ScriptKernelID : public ObjectBase {
public:
    ScriptKernelID(Context *rsc, Script *s, int slot, int sig);
    virtual ~ScriptKernelID();

    virtual void serialize(Context *rsc, OStream *stream) const;
    virtual RsA3DClassID getClassId() const;

    Script *mScript;
    int mSlot;
    bool mHasKernelInput;
    bool mHasKernelOutput;
};

class ScriptFieldID : public ObjectBase {
public:
    ScriptFieldID(Context *rsc, Script *s, int slot);
    virtual ~ScriptFieldID();

    virtual void serialize(Context *rsc, OStream *stream) const;
    virtual RsA3DClassID getClassId() const;

    Script *mScript;
    int mSlot;
};

class Script : public ObjectBase {
public:

    struct Hal {
        void * drv;

        struct DriverInfo {
            int mVersionMajor;
            int mVersionMinor;

            size_t exportedVariableCount;
            size_t exportedFunctionCount;
            size_t exportedPragmaCount;
            char const **exportedPragmaKeyList;
            char const **exportedPragmaValueList;
            const std::pair<const char *, uint32_t> *exportedForeachFuncList;

            int (* root)();
        };
        DriverInfo info;

        void * IMGPrivateData;
    };
    Hal mHal;

    typedef void (* InvokeFunc_t)(void);

    Script(Context *);
    virtual ~Script();

    struct Enviroment_t {
        int64_t mStartTimeMillis;
        mutable int64_t mLastDtTime;

#ifndef RS_COMPATIBILITY_LIB
        ObjectBaseRef<ProgramVertex> mVertex;
        ObjectBaseRef<ProgramFragment> mFragment;
        ObjectBaseRef<ProgramRaster> mRaster;
        ObjectBaseRef<ProgramStore> mFragmentStore;
#endif
    };
    Enviroment_t mEnviroment;

    void setSlot(uint32_t slot, Allocation *a);
    void setVar(uint32_t slot, const void *val, size_t len);
    void getVar(uint32_t slot, const void *val, size_t len);
    void setVar(uint32_t slot, const void *val, size_t len, Element *e,
                const size_t *dims, size_t dimLen);
    void setVarObj(uint32_t slot, ObjectBase *val);

    virtual bool freeChildren();

    virtual void runForEach(Context *rsc,
                            uint32_t slot,
                            const Allocation * ain,
                            Allocation * aout,
                            const void * usr,
                            size_t usrBytes,
                            const RsScriptCall *sc = NULL) = 0;

    virtual void Invoke(Context *rsc, uint32_t slot, const void *data, size_t len) = 0;
    virtual void setupScript(Context *rsc) = 0;
    virtual uint32_t run(Context *) = 0;

    bool hasObjectSlots() const {
        return mHasObjectSlots;
    }
protected:
    bool mInitialized;
    bool mHasObjectSlots;
    ObjectBaseRef<Allocation> *mSlots;
    ObjectBaseRef<const Type> *mTypes;

};


}
}
#endif

