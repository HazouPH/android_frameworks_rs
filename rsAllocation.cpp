/*
 * Copyright (C) 2013 The Android Open Source Project
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

#include "rsContext.h"
#include "rsAllocation.h"
#include "rsAdapter.h"
#include "rs_hal.h"

#if !defined(RS_SERVER) && !defined(RS_COMPATIBILITY_LIB)
#include "system/window.h"
#include "gui/GLConsumer.h"
#endif

using namespace android;
using namespace android::renderscript;

Allocation::Allocation(Context *rsc, const Type *type, uint32_t usages,
                       RsAllocationMipmapControl mc, void * ptr)
    : ObjectBase(rsc) {

    memset(&mHal, 0, sizeof(mHal));
    mHal.state.mipmapControl = RS_ALLOCATION_MIPMAP_NONE;
    mHal.state.usageFlags = usages;
    mHal.state.mipmapControl = mc;
    mHal.state.userProvidedPtr = ptr;

    setType(type);
    updateCache();
}

void Allocation::operator delete(void* ptr) {
    if (ptr) {
        Allocation *a = (Allocation*) ptr;
        a->getContext()->mHal.funcs.freeRuntimeMem(ptr);
    }
}

Allocation * Allocation::createAllocation(Context *rsc, const Type *type, uint32_t usages,
                              RsAllocationMipmapControl mc, void * ptr) {
    // Allocation objects must use allocator specified by the driver
    void* allocMem = rsc->mHal.funcs.allocRuntimeMem(sizeof(Allocation), 0);

    if (!allocMem) {
        rsc->setError(RS_ERROR_FATAL_DRIVER, "Couldn't allocate memory for Allocation");
        return NULL;
    }

    Allocation *a = new (allocMem) Allocation(rsc, type, usages, mc, ptr);

    if (!rsc->mHal.funcs.allocation.init(rsc, a, type->getElement()->getHasReferences())) {
        rsc->setError(RS_ERROR_FATAL_DRIVER, "Allocation::Allocation, alloc failure");
        delete a;
        return NULL;
    }

    return a;
}

void Allocation::updateCache() {
    const Type *type = mHal.state.type;
    mHal.state.yuv = type->getDimYuv();
    mHal.state.hasFaces = type->getDimFaces();
    mHal.state.hasMipmaps = type->getDimLOD();
    mHal.state.elementSizeBytes = type->getElementSizeBytes();
    mHal.state.hasReferences = mHal.state.type->getElement()->getHasReferences();
}

Allocation::~Allocation() {
#if !defined(RS_SERVER) && !defined(RS_COMPATIBILITY_LIB)
    if (mGrallocConsumer.get()) {
        mGrallocConsumer->unlockBuffer();
        mGrallocConsumer = NULL;
    }
#endif

    freeChildrenUnlocked();
    mRSC->mHal.funcs.allocation.destroy(mRSC, this);
}

void Allocation::syncAll(Context *rsc, RsAllocationUsageType src) {
    rsc->mHal.funcs.allocation.syncAll(rsc, this, src);
}

void Allocation::data(Context *rsc, uint32_t xoff, uint32_t lod,
                         uint32_t count, const void *data, size_t sizeBytes) {
    const size_t eSize = mHal.state.type->getElementSizeBytes();

    if ((count * eSize) != sizeBytes) {
        ALOGE("Allocation::subData called with mismatched size expected %zu, got %zu",
             (count * eSize), sizeBytes);
        mHal.state.type->dumpLOGV("type info");
        return;
    }

    rsc->mHal.funcs.allocation.data1D(rsc, this, xoff, lod, count, data, sizeBytes);
    sendDirty(rsc);
}

void Allocation::data(Context *rsc, uint32_t xoff, uint32_t yoff, uint32_t lod, RsAllocationCubemapFace face,
                      uint32_t w, uint32_t h, const void *data, size_t sizeBytes, size_t stride) {
    rsc->mHal.funcs.allocation.data2D(rsc, this, xoff, yoff, lod, face, w, h, data, sizeBytes, stride);
    sendDirty(rsc);
}

void Allocation::data(Context *rsc, uint32_t xoff, uint32_t yoff, uint32_t zoff,
                      uint32_t lod,
                      uint32_t w, uint32_t h, uint32_t d, const void *data, size_t sizeBytes, size_t stride) {
    rsc->mHal.funcs.allocation.data3D(rsc, this, xoff, yoff, zoff, lod, w, h, d, data, sizeBytes, stride);
    sendDirty(rsc);
}

void Allocation::read(Context *rsc, uint32_t xoff, uint32_t lod,
                      uint32_t count, void *data, size_t sizeBytes) {
    const size_t eSize = mHal.state.type->getElementSizeBytes();

    if ((count * eSize) != sizeBytes) {
        ALOGE("Allocation::read called with mismatched size expected %zu, got %zu",
             (count * eSize), sizeBytes);
        mHal.state.type->dumpLOGV("type info");
        return;
    }

    rsc->mHal.funcs.allocation.read1D(rsc, this, xoff, lod, count, data, sizeBytes);
}

void Allocation::read(Context *rsc, uint32_t xoff, uint32_t yoff, uint32_t lod, RsAllocationCubemapFace face,
                      uint32_t w, uint32_t h, void *data, size_t sizeBytes, size_t stride) {
    const size_t eSize = mHal.state.elementSizeBytes;
    const size_t lineSize = eSize * w;
    if (!stride) {
        stride = lineSize;
    } else {
        if ((lineSize * h) != sizeBytes) {
            ALOGE("Allocation size mismatch, expected %zu, got %zu", (lineSize * h), sizeBytes);
            rsAssert(!"Allocation::read called with mismatched size");
            return;
        }
    }

    rsc->mHal.funcs.allocation.read2D(rsc, this, xoff, yoff, lod, face, w, h, data, sizeBytes, stride);
}

void Allocation::read(Context *rsc, uint32_t xoff, uint32_t yoff, uint32_t zoff, uint32_t lod,
                      uint32_t w, uint32_t h, uint32_t d, void *data, size_t sizeBytes, size_t stride) {
    const size_t eSize = mHal.state.elementSizeBytes;
    const size_t lineSize = eSize * w;
    if (!stride) {
        stride = lineSize;
    }

    rsc->mHal.funcs.allocation.read3D(rsc, this, xoff, yoff, zoff, lod, w, h, d, data, sizeBytes, stride);

}

void Allocation::elementData(Context *rsc, uint32_t x, const void *data,
                                uint32_t cIdx, size_t sizeBytes) {
    size_t eSize = mHal.state.elementSizeBytes;

    if (cIdx >= mHal.state.type->getElement()->getFieldCount()) {
        ALOGE("Error Allocation::subElementData component %i out of range.", cIdx);
        rsc->setError(RS_ERROR_BAD_VALUE, "subElementData component out of range.");
        return;
    }

    if (x >= mHal.drvState.lod[0].dimX) {
        ALOGE("Error Allocation::subElementData X offset %i out of range.", x);
        rsc->setError(RS_ERROR_BAD_VALUE, "subElementData X offset out of range.");
        return;
    }

    const Element * e = mHal.state.type->getElement()->getField(cIdx);
    uint32_t elemArraySize = mHal.state.type->getElement()->getFieldArraySize(cIdx);
    if (sizeBytes != e->getSizeBytes() * elemArraySize) {
        ALOGE("Error Allocation::subElementData data size %zu does not match field size %zu.", sizeBytes, e->getSizeBytes());
        rsc->setError(RS_ERROR_BAD_VALUE, "subElementData bad size.");
        return;
    }

    rsc->mHal.funcs.allocation.elementData1D(rsc, this, x, data, cIdx, sizeBytes);
    sendDirty(rsc);
}

void Allocation::elementData(Context *rsc, uint32_t x, uint32_t y,
                                const void *data, uint32_t cIdx, size_t sizeBytes) {
    size_t eSize = mHal.state.elementSizeBytes;

    if (x >= mHal.drvState.lod[0].dimX) {
        ALOGE("Error Allocation::subElementData X offset %i out of range.", x);
        rsc->setError(RS_ERROR_BAD_VALUE, "subElementData X offset out of range.");
        return;
    }

    if (y >= mHal.drvState.lod[0].dimY) {
        ALOGE("Error Allocation::subElementData X offset %i out of range.", x);
        rsc->setError(RS_ERROR_BAD_VALUE, "subElementData X offset out of range.");
        return;
    }

    if (cIdx >= mHal.state.type->getElement()->getFieldCount()) {
        ALOGE("Error Allocation::subElementData component %i out of range.", cIdx);
        rsc->setError(RS_ERROR_BAD_VALUE, "subElementData component out of range.");
        return;
    }

    const Element * e = mHal.state.type->getElement()->getField(cIdx);
    uint32_t elemArraySize = mHal.state.type->getElement()->getFieldArraySize(cIdx);
    if (sizeBytes != e->getSizeBytes() * elemArraySize) {
        ALOGE("Error Allocation::subElementData data size %zu does not match field size %zu.", sizeBytes, e->getSizeBytes());
        rsc->setError(RS_ERROR_BAD_VALUE, "subElementData bad size.");
        return;
    }

    rsc->mHal.funcs.allocation.elementData2D(rsc, this, x, y, data, cIdx, sizeBytes);
    sendDirty(rsc);
}

void Allocation::addProgramToDirty(const Program *p) {
    mToDirtyList.push(p);
}

void Allocation::removeProgramToDirty(const Program *p) {
    for (size_t ct=0; ct < mToDirtyList.size(); ct++) {
        if (mToDirtyList[ct] == p) {
            mToDirtyList.removeAt(ct);
            return;
        }
    }
    rsAssert(0);
}

void Allocation::dumpLOGV(const char *prefix) const {
    ObjectBase::dumpLOGV(prefix);
    char buf[1024];

    if ((strlen(prefix) + 10) < sizeof(buf)) {
        sprintf(buf, "%s type ", prefix);
        if (mHal.state.type) {
            mHal.state.type->dumpLOGV(buf);
        }
    }
    ALOGV("%s allocation ptr=%p  mUsageFlags=0x04%x, mMipmapControl=0x%04x",
         prefix, mHal.drvState.lod[0].mallocPtr, mHal.state.usageFlags, mHal.state.mipmapControl);
}

uint32_t Allocation::getPackedSize() const {
    uint32_t numItems = mHal.state.type->getCellCount();
    return numItems * mHal.state.type->getElement()->getSizeBytesUnpadded();
}

void Allocation::writePackedData(Context *rsc, const Type *type,
                                 uint8_t *dst, const uint8_t *src, bool dstPadded) {
    const Element *elem = type->getElement();
    uint32_t unpaddedBytes = elem->getSizeBytesUnpadded();
    uint32_t paddedBytes = elem->getSizeBytes();
    uint32_t numItems = type->getPackedSizeBytes() / paddedBytes;

    uint32_t srcInc = !dstPadded ? paddedBytes : unpaddedBytes;
    uint32_t dstInc =  dstPadded ? paddedBytes : unpaddedBytes;

    // no sub-elements
    uint32_t fieldCount = elem->getFieldCount();
    if (fieldCount == 0) {
        for (uint32_t i = 0; i < numItems; i ++) {
            memcpy(dst, src, unpaddedBytes);
            src += srcInc;
            dst += dstInc;
        }
        return;
    }

    // Cache offsets
    uint32_t *offsetsPadded = new uint32_t[fieldCount];
    uint32_t *offsetsUnpadded = new uint32_t[fieldCount];
    uint32_t *sizeUnpadded = new uint32_t[fieldCount];

    for (uint32_t i = 0; i < fieldCount; i++) {
        offsetsPadded[i] = elem->getFieldOffsetBytes(i);
        offsetsUnpadded[i] = elem->getFieldOffsetBytesUnpadded(i);
        sizeUnpadded[i] = elem->getField(i)->getSizeBytesUnpadded();
    }

    uint32_t *srcOffsets = !dstPadded ? offsetsPadded : offsetsUnpadded;
    uint32_t *dstOffsets =  dstPadded ? offsetsPadded : offsetsUnpadded;

    // complex elements, need to copy subelem after subelem
    for (uint32_t i = 0; i < numItems; i ++) {
        for (uint32_t fI = 0; fI < fieldCount; fI++) {
            memcpy(dst + dstOffsets[fI], src + srcOffsets[fI], sizeUnpadded[fI]);
        }
        src += srcInc;
        dst += dstInc;
    }

    delete[] offsetsPadded;
    delete[] offsetsUnpadded;
    delete[] sizeUnpadded;
}

void Allocation::unpackVec3Allocation(Context *rsc, const void *data, size_t dataSize) {
    const uint8_t *src = (const uint8_t*)data;
    uint8_t *dst = (uint8_t *)rsc->mHal.funcs.allocation.lock1D(rsc, this);

    writePackedData(rsc, getType(), dst, src, true);
    rsc->mHal.funcs.allocation.unlock1D(rsc, this);
}

void Allocation::packVec3Allocation(Context *rsc, OStream *stream) const {
    uint32_t paddedBytes = getType()->getElement()->getSizeBytes();
    uint32_t unpaddedBytes = getType()->getElement()->getSizeBytesUnpadded();
    uint32_t numItems = mHal.state.type->getCellCount();

    const uint8_t *src = (const uint8_t*)rsc->mHal.funcs.allocation.lock1D(rsc, this);
    uint8_t *dst = new uint8_t[numItems * unpaddedBytes];

    writePackedData(rsc, getType(), dst, src, false);
    stream->addByteArray(dst, getPackedSize());

    delete[] dst;
    rsc->mHal.funcs.allocation.unlock1D(rsc, this);
}

void Allocation::serialize(Context *rsc, OStream *stream) const {
    // Need to identify ourselves
    stream->addU32((uint32_t)getClassId());
    stream->addString(getName());

    // First thing we need to serialize is the type object since it will be needed
    // to initialize the class
    mHal.state.type->serialize(rsc, stream);

    uint32_t dataSize = mHal.state.type->getPackedSizeBytes();
    // 3 element vectors are padded to 4 in memory, but padding isn't serialized
    uint32_t packedSize = getPackedSize();
    // Write how much data we are storing
    stream->addU32(packedSize);
    if (dataSize == packedSize) {
        // Now write the data
        stream->addByteArray(rsc->mHal.funcs.allocation.lock1D(rsc, this), dataSize);
        rsc->mHal.funcs.allocation.unlock1D(rsc, this);
    } else {
        // Now write the data
        packVec3Allocation(rsc, stream);
    }
}

Allocation *Allocation::createFromStream(Context *rsc, IStream *stream) {
    // First make sure we are reading the correct object
    RsA3DClassID classID = (RsA3DClassID)stream->loadU32();
    if (classID != RS_A3D_CLASS_ID_ALLOCATION) {
        ALOGE("allocation loading skipped due to invalid class id\n");
        return NULL;
    }

    const char *name = stream->loadString();

    Type *type = Type::createFromStream(rsc, stream);
    if (!type) {
        return NULL;
    }
    type->compute();

    Allocation *alloc = Allocation::createAllocation(rsc, type, RS_ALLOCATION_USAGE_SCRIPT);
    type->decUserRef();

    // Number of bytes we wrote out for this allocation
    uint32_t dataSize = stream->loadU32();
    // 3 element vectors are padded to 4 in memory, but padding isn't serialized
    uint32_t packedSize = alloc->getPackedSize();
    if (dataSize != type->getPackedSizeBytes() &&
        dataSize != packedSize) {
        ALOGE("failed to read allocation because numbytes written is not the same loaded type wants\n");
        ObjectBase::checkDelete(alloc);
        ObjectBase::checkDelete(type);
        return NULL;
    }

    alloc->assignName(name);
    if (dataSize == type->getPackedSizeBytes()) {
        uint32_t count = dataSize / type->getElementSizeBytes();
        // Read in all of our allocation data
        alloc->data(rsc, 0, 0, count, stream->getPtr() + stream->getPos(), dataSize);
    } else {
        alloc->unpackVec3Allocation(rsc, stream->getPtr() + stream->getPos(), dataSize);
    }
    stream->reset(stream->getPos() + dataSize);

    return alloc;
}

void Allocation::sendDirty(const Context *rsc) const {
#ifndef RS_COMPATIBILITY_LIB
    for (size_t ct=0; ct < mToDirtyList.size(); ct++) {
        mToDirtyList[ct]->forceDirty();
    }
#endif
    mRSC->mHal.funcs.allocation.markDirty(rsc, this);
}

void Allocation::incRefs(const void *ptr, size_t ct, size_t startOff) const {
    mHal.state.type->incRefs(ptr, ct, startOff);
}

void Allocation::decRefs(const void *ptr, size_t ct, size_t startOff) const {
    if (!mHal.state.hasReferences || !getIsScript()) {
        return;
    }
    mHal.state.type->decRefs(ptr, ct, startOff);
}

void Allocation::freeChildrenUnlocked () {
    void *ptr = mRSC->mHal.funcs.allocation.lock1D(mRSC, this);
    decRefs(ptr, mHal.state.type->getCellCount(), 0);
    mRSC->mHal.funcs.allocation.unlock1D(mRSC, this);
}

bool Allocation::freeChildren() {
    if (mHal.state.hasReferences) {
        incSysRef();
        freeChildrenUnlocked();
        return decSysRef();
    }
    return false;
}

void Allocation::copyRange1D(Context *rsc, const Allocation *src, int32_t srcOff, int32_t destOff, int32_t len) {
}

void Allocation::resize1D(Context *rsc, uint32_t dimX) {
    uint32_t oldDimX = mHal.drvState.lod[0].dimX;
    if (dimX == oldDimX) {
        return;
    }

    ObjectBaseRef<Type> t = mHal.state.type->cloneAndResize1D(rsc, dimX);
    if (dimX < oldDimX) {
        decRefs(rsc->mHal.funcs.allocation.lock1D(rsc, this), oldDimX - dimX, dimX);
        rsc->mHal.funcs.allocation.unlock1D(rsc, this);
    }
    rsc->mHal.funcs.allocation.resize(rsc, this, t.get(), mHal.state.hasReferences);
    setType(t.get());
    updateCache();
}

void Allocation::resize2D(Context *rsc, uint32_t dimX, uint32_t dimY) {
    ALOGE("not implemented");
}

#ifndef RS_COMPATIBILITY_LIB
void Allocation::NewBufferListener::onFrameAvailable() {
    intptr_t ip = (intptr_t)alloc;
    rsc->sendMessageToClient(NULL, RS_MESSAGE_TO_CLIENT_NEW_BUFFER, ip, 0, true);
}
#endif

void * Allocation::getSurface(const Context *rsc) {
#ifndef RS_COMPATIBILITY_LIB
    // Configure GrallocConsumer to be in asynchronous mode
    sp<BufferQueue> bq = new BufferQueue();
    mGrallocConsumer = new GrallocConsumer(this, bq);
    sp<IGraphicBufferProducer> bp = bq;
    bp->incStrong(NULL);

    mBufferListener = new NewBufferListener();
    mBufferListener->rsc = rsc;
    mBufferListener->alloc = this;

    mGrallocConsumer->setFrameAvailableListener(mBufferListener);
    return bp.get();
#else
    return NULL;
#endif
    //return rsc->mHal.funcs.allocation.getSurface(rsc, this);
}

void Allocation::setSurface(const Context *rsc, RsNativeWindow sur) {
    ANativeWindow *nw = (ANativeWindow *)sur;
    rsc->mHal.funcs.allocation.setSurface(rsc, this, nw);
}

void Allocation::ioSend(const Context *rsc) {
    rsc->mHal.funcs.allocation.ioSend(rsc, this);
}

void Allocation::ioReceive(const Context *rsc) {
    void *ptr = NULL;
    size_t stride = 0;
#ifndef RS_COMPATIBILITY_LIB
    if (mHal.state.usageFlags & RS_ALLOCATION_USAGE_SCRIPT) {
        status_t ret = mGrallocConsumer->lockNextBuffer();

        if (ret == OK) {
            rsc->mHal.funcs.allocation.ioReceive(rsc, this);
        } else if (ret == BAD_VALUE) {
            // No new frame, don't do anything
        } else {
            rsc->setError(RS_ERROR_DRIVER, "Error receiving IO input buffer.");
        }

    }
#endif
}

void Allocation::destroy(const Context *rsc) {
    freeChildrenUnlocked();
    rsc->mHal.funcs.allocation.destroy(rsc, this);
}

/////////////////
//

namespace android {
namespace renderscript {

void rsi_AllocationSyncAll(Context *rsc, RsAllocation va, RsAllocationUsageType src) {
    Allocation *a = static_cast<Allocation *>(va);
    a->sendDirty(rsc);
    a->syncAll(rsc, src);
}

void rsi_AllocationGenerateMipmaps(Context *rsc, RsAllocation va) {
    Allocation *alloc = static_cast<Allocation *>(va);
    rsc->mHal.funcs.allocation.generateMipmaps(rsc, alloc);
}

void rsi_AllocationCopyToBitmap(Context *rsc, RsAllocation va, void *data, size_t sizeBytes) {
    Allocation *a = static_cast<Allocation *>(va);
    const Type * t = a->getType();
    a->read(rsc, 0, 0, 0, RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_X,
            t->getDimX(), t->getDimY(), data, sizeBytes, 0);
}

void rsi_Allocation1DData(Context *rsc, RsAllocation va, uint32_t xoff, uint32_t lod,
                          uint32_t count, const void *data, size_t sizeBytes) {
    Allocation *a = static_cast<Allocation *>(va);
    a->data(rsc, xoff, lod, count, data, sizeBytes);
}

void rsi_Allocation2DElementData(Context *rsc, RsAllocation va, uint32_t x, uint32_t y, uint32_t lod, RsAllocationCubemapFace face,
                                 const void *data, size_t sizeBytes, size_t eoff) {
    Allocation *a = static_cast<Allocation *>(va);
    a->elementData(rsc, x, y, data, eoff, sizeBytes);
}

void rsi_Allocation1DElementData(Context *rsc, RsAllocation va, uint32_t x, uint32_t lod,
                                 const void *data, size_t sizeBytes, size_t eoff) {
    Allocation *a = static_cast<Allocation *>(va);
    a->elementData(rsc, x, data, eoff, sizeBytes);
}

void rsi_Allocation2DData(Context *rsc, RsAllocation va, uint32_t xoff, uint32_t yoff, uint32_t lod, RsAllocationCubemapFace face,
                          uint32_t w, uint32_t h, const void *data, size_t sizeBytes, size_t stride) {
    Allocation *a = static_cast<Allocation *>(va);
    a->data(rsc, xoff, yoff, lod, face, w, h, data, sizeBytes, stride);
}

void rsi_Allocation3DData(Context *rsc, RsAllocation va, uint32_t xoff, uint32_t yoff, uint32_t zoff, uint32_t lod,
                          uint32_t w, uint32_t h, uint32_t d, const void *data, size_t sizeBytes, size_t stride) {
    Allocation *a = static_cast<Allocation *>(va);
    a->data(rsc, xoff, yoff, zoff, lod, w, h, d, data, sizeBytes, stride);
}


void rsi_AllocationRead(Context *rsc, RsAllocation va, void *data, size_t sizeBytes) {
    Allocation *a = static_cast<Allocation *>(va);
    const Type * t = a->getType();
    if(t->getDimY()) {
        a->read(rsc, 0, 0, 0, RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_X,
                t->getDimX(), t->getDimY(), data, sizeBytes, 0);
    } else {
        a->read(rsc, 0, 0, t->getDimX(), data, sizeBytes);
    }

}

void rsi_AllocationResize1D(Context *rsc, RsAllocation va, uint32_t dimX) {
    Allocation *a = static_cast<Allocation *>(va);
    a->resize1D(rsc, dimX);
}

void rsi_AllocationResize2D(Context *rsc, RsAllocation va, uint32_t dimX, uint32_t dimY) {
    Allocation *a = static_cast<Allocation *>(va);
    a->resize2D(rsc, dimX, dimY);
}

RsAllocation rsi_AllocationCreateTyped(Context *rsc, RsType vtype,
                                       RsAllocationMipmapControl mips,
                                       uint32_t usages, uintptr_t ptr) {
    Allocation * alloc = Allocation::createAllocation(rsc, static_cast<Type *>(vtype), usages, mips, (void*)ptr);
    if (!alloc) {
        return NULL;
    }
    alloc->incUserRef();
    return alloc;
}

RsAllocation rsi_AllocationCreateFromBitmap(Context *rsc, RsType vtype,
                                            RsAllocationMipmapControl mips,
                                            const void *data, size_t sizeBytes, uint32_t usages) {
    Type *t = static_cast<Type *>(vtype);

    RsAllocation vTexAlloc = rsi_AllocationCreateTyped(rsc, vtype, mips, usages, 0);
    Allocation *texAlloc = static_cast<Allocation *>(vTexAlloc);
    if (texAlloc == NULL) {
        ALOGE("Memory allocation failure");
        return NULL;
    }

    texAlloc->data(rsc, 0, 0, 0, RS_ALLOCATION_CUBEMAP_FACE_POSITIVE_X,
                   t->getDimX(), t->getDimY(), data, sizeBytes, 0);
    if (mips == RS_ALLOCATION_MIPMAP_FULL) {
        rsc->mHal.funcs.allocation.generateMipmaps(rsc, texAlloc);
    }

    texAlloc->sendDirty(rsc);
    return texAlloc;
}

RsAllocation rsi_AllocationCubeCreateFromBitmap(Context *rsc, RsType vtype,
                                                RsAllocationMipmapControl mips,
                                                const void *data, size_t sizeBytes, uint32_t usages) {
    Type *t = static_cast<Type *>(vtype);

    // Cubemap allocation's faces should be Width by Width each.
    // Source data should have 6 * Width by Width pixels
    // Error checking is done in the java layer
    RsAllocation vTexAlloc = rsi_AllocationCreateTyped(rsc, vtype, mips, usages, 0);
    Allocation *texAlloc = static_cast<Allocation *>(vTexAlloc);
    if (texAlloc == NULL) {
        ALOGE("Memory allocation failure");
        return NULL;
    }

    uint32_t faceSize = t->getDimX();
    uint32_t strideBytes = faceSize * 6 * t->getElementSizeBytes();
    uint32_t copySize = faceSize * t->getElementSizeBytes();

    uint8_t *sourcePtr = (uint8_t*)data;
    for (uint32_t face = 0; face < 6; face ++) {
        for (uint32_t dI = 0; dI < faceSize; dI ++) {
            texAlloc->data(rsc, 0, dI, 0, (RsAllocationCubemapFace)face,
                           t->getDimX(), 1, sourcePtr + strideBytes * dI, copySize, 0);
        }

        // Move the data pointer to the next cube face
        sourcePtr += copySize;
    }

    if (mips == RS_ALLOCATION_MIPMAP_FULL) {
        rsc->mHal.funcs.allocation.generateMipmaps(rsc, texAlloc);
    }

    texAlloc->sendDirty(rsc);
    return texAlloc;
}

void rsi_AllocationCopy2DRange(Context *rsc,
                               RsAllocation dstAlloc,
                               uint32_t dstXoff, uint32_t dstYoff,
                               uint32_t dstMip, uint32_t dstFace,
                               uint32_t width, uint32_t height,
                               RsAllocation srcAlloc,
                               uint32_t srcXoff, uint32_t srcYoff,
                               uint32_t srcMip, uint32_t srcFace) {
    Allocation *dst = static_cast<Allocation *>(dstAlloc);
    Allocation *src= static_cast<Allocation *>(srcAlloc);
    rsc->mHal.funcs.allocation.allocData2D(rsc, dst, dstXoff, dstYoff, dstMip,
                                           (RsAllocationCubemapFace)dstFace,
                                           width, height,
                                           src, srcXoff, srcYoff,srcMip,
                                           (RsAllocationCubemapFace)srcFace);
}

void rsi_AllocationCopy3DRange(Context *rsc,
                               RsAllocation dstAlloc,
                               uint32_t dstXoff, uint32_t dstYoff, uint32_t dstZoff,
                               uint32_t dstMip,
                               uint32_t width, uint32_t height, uint32_t depth,
                               RsAllocation srcAlloc,
                               uint32_t srcXoff, uint32_t srcYoff, uint32_t srcZoff,
                               uint32_t srcMip) {
    Allocation *dst = static_cast<Allocation *>(dstAlloc);
    Allocation *src= static_cast<Allocation *>(srcAlloc);
    rsc->mHal.funcs.allocation.allocData3D(rsc, dst, dstXoff, dstYoff, dstZoff, dstMip,
                                           width, height, depth,
                                           src, srcXoff, srcYoff, srcZoff, srcMip);
}


void * rsi_AllocationGetSurface(Context *rsc, RsAllocation valloc) {
    Allocation *alloc = static_cast<Allocation *>(valloc);
    void *s = alloc->getSurface(rsc);
    return s;
}

void rsi_AllocationSetSurface(Context *rsc, RsAllocation valloc, RsNativeWindow sur) {
    Allocation *alloc = static_cast<Allocation *>(valloc);
    alloc->setSurface(rsc, sur);
}

void rsi_AllocationIoSend(Context *rsc, RsAllocation valloc) {
    Allocation *alloc = static_cast<Allocation *>(valloc);
    alloc->ioSend(rsc);
}

void rsi_AllocationIoReceive(Context *rsc, RsAllocation valloc) {
    Allocation *alloc = static_cast<Allocation *>(valloc);
    alloc->ioReceive(rsc);
}

void rsi_AllocationDestroy(Context *rsc, RsAllocation valloc) {
    Allocation *alloc = static_cast<Allocation *>(valloc);
    alloc->destroy(rsc);
}

void rsi_Allocation1DRead(Context *rsc, RsAllocation va, uint32_t xoff, uint32_t lod,
                          uint32_t count, void *data, size_t sizeBytes) {
    Allocation *a = static_cast<Allocation *>(va);
    rsc->mHal.funcs.allocation.read1D(rsc, a, xoff, lod, count, data, sizeBytes);
}

void rsi_Allocation2DRead(Context *rsc, RsAllocation va, uint32_t xoff, uint32_t yoff,
                          uint32_t lod, RsAllocationCubemapFace face, uint32_t w,
                          uint32_t h, void *data, size_t sizeBytes, size_t stride) {
    Allocation *a = static_cast<Allocation *>(va);
    a->read(rsc, xoff, yoff, lod, face, w, h, data, sizeBytes, stride);
}

}
}

extern "C" const void * rsaAllocationGetType(RsContext con, RsAllocation va) {
    Allocation *a = static_cast<Allocation *>(va);
    a->getType()->incUserRef();

    return a->getType();
}
