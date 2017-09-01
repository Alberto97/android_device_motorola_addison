/*
 * Copyright (C) 2017 The LineageOS Project
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

#include <media/MediaCodecBuffer.h>
#include <media/stagefright/foundation/AHandler.h>
#include <utils/Vector.h>

namespace android {

extern "C" void _ZN7android10MediaCodec12CreateByTypeERKNS_2spINS_7ALooperEEERKNS_7AStringEbPiij(
	const sp<ALooper> &looper, const AString &mime, bool encoder, status_t *err, pid_t pid, uid_t uid);

extern "C" void _ZNK7android10MediaCodec15getInputBuffersEPNS_6VectorINS_2spINS_16MediaCodecBufferEEEEE(Vector<sp<ABuffer> > *buffers); //Vector<sp<MediaCodecBuffer> > *buffers
extern "C" void _ZNK7android10MediaCodec16getOutputBuffersEPNS_6VectorINS_2spINS_16MediaCodecBufferEEEEE(Vector<sp<ABuffer> > *buffers); //Vector<sp<MediaCodecBuffer> > *buffers

extern "C" void _ZN7android10MediaCodec12CreateByTypeERKNS_2spINS_7ALooperEEERKNS_7AStringEbPii(
	const sp<ALooper> &looper, const AString &mime, bool encoder, status_t *err, pid_t pid) {
	_ZN7android10MediaCodec12CreateByTypeERKNS_2spINS_7ALooperEEERKNS_7AStringEbPiij(
		looper, mime, encoder, err, pid, 0);
}

extern "C" void _ZNK7android10MediaCodec15getInputBuffersEPNS_6VectorINS_2spINS_7ABufferEEEEE(Vector<sp<ABuffer> > *buffers) {
	_ZNK7android10MediaCodec15getInputBuffersEPNS_6VectorINS_2spINS_16MediaCodecBufferEEEEE(buffers);
}

extern "C" void _ZNK7android10MediaCodec16getOutputBuffersEPNS_6VectorINS_2spINS_7ABufferEEEEE(Vector<sp<ABuffer> > *buffers) {
	_ZNK7android10MediaCodec16getOutputBuffersEPNS_6VectorINS_2spINS_16MediaCodecBufferEEEEE(buffers);
}
}
