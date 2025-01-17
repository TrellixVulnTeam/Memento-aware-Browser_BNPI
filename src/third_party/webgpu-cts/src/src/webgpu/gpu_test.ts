import { Fixture } from '../common/framework/fixture.js';
import { compileGLSL, initGLSL } from '../common/framework/glsl.js';
import { DevicePool, TestOOMedShouldAttemptGC } from '../common/framework/gpu/device_pool.js';
import { attemptGarbageCollection } from '../common/framework/util/collect_garbage.js';
import { assert } from '../common/framework/util/util.js';

import {
  fillTextureDataWithTexelValue,
  getTextureCopyLayout,
  LayoutOptions as TextureLayoutOptions,
} from './util/texture/layout.js';
import { PerTexelComponent, getTexelDataRepresentation } from './util/texture/texelData.js';

type ShaderStage = import('@webgpu/glslang/dist/web-devel/glslang').ShaderStage;

type TypedArrayBufferView =
  | Uint8Array
  | Uint16Array
  | Uint32Array
  | Int8Array
  | Int16Array
  | Int32Array
  | Float32Array
  | Float64Array;

type TypedArrayBufferViewConstructor =
  | Uint8ArrayConstructor
  | Uint16ArrayConstructor
  | Uint32ArrayConstructor
  | Int8ArrayConstructor
  | Int16ArrayConstructor
  | Int32ArrayConstructor
  | Float32ArrayConstructor
  | Float64ArrayConstructor;

const devicePool = new DevicePool();

export class GPUTest extends Fixture {
  private objects: { device: GPUDevice; queue: GPUQueue } | undefined = undefined;
  initialized = false;

  get device(): GPUDevice {
    assert(this.objects !== undefined);
    return this.objects.device;
  }

  get queue(): GPUQueue {
    assert(this.objects !== undefined);
    return this.objects.queue;
  }

  async init(): Promise<void> {
    await super.init();
    await initGLSL();

    const device = await devicePool.acquire();
    const queue = device.defaultQueue;
    this.objects = { device, queue };
  }

  // Note: finalize is called even if init was unsuccessful.
  async finalize(): Promise<void> {
    await super.finalize();

    if (this.objects) {
      let threw: undefined | Error;
      {
        const objects = this.objects;
        this.objects = undefined;
        try {
          await devicePool.release(objects.device);
        } catch (ex) {
          threw = ex;
        }
      }
      // The GPUDevice and GPUQueue should now have no outstanding references.

      if (threw) {
        if (threw instanceof TestOOMedShouldAttemptGC) {
          // Try to clean up, in case there are stray GPU resources in need of collection.
          await attemptGarbageCollection();
        }
        throw threw;
      }
    }
  }

  makeShaderModule(stage: ShaderStage, code: { glsl: string } | { wgsl: string }): GPUShaderModule {
    // If both are provided, always choose WGSL. (Can change this if needed.)
    if ('wgsl' in code) {
      return this.device.createShaderModule({ code: code.wgsl });
    } else {
      const spirv = compileGLSL(code.glsl, stage, false);
      return this.device.createShaderModule({ code: spirv });
    }
  }

  createCopyForMapRead(src: GPUBuffer, start: number, size: number): GPUBuffer {
    const dst = this.device.createBuffer({
      size,
      usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST,
    });

    const c = this.device.createCommandEncoder();
    c.copyBufferToBuffer(src, start, dst, 0, size);

    this.queue.submit([c.finish()]);

    return dst;
  }

  // TODO: add an expectContents for textures, which logs data: uris on failure

  expectContents(src: GPUBuffer, expected: TypedArrayBufferView): void {
    this.expectSubContents(src, 0, expected);
  }

  expectSubContents(src: GPUBuffer, start: number, expected: TypedArrayBufferView): void {
    const dst = this.createCopyForMapRead(src, start, expected.buffer.byteLength);

    this.eventualAsyncExpectation(async niceStack => {
      const constructor = expected.constructor as TypedArrayBufferViewConstructor;
      const actual = new constructor(await dst.mapReadAsync());
      const check = this.checkBuffer(actual, expected);
      if (check !== undefined) {
        niceStack.message = check;
        this.rec.expectationFailed(niceStack);
      }
      dst.destroy();
    });
  }

  expectBuffer(actual: Uint8Array, exp: Uint8Array): void {
    const check = this.checkBuffer(actual, exp);
    if (check !== undefined) {
      this.rec.expectationFailed(new Error(check));
    }
  }

  checkBuffer(
    actual: TypedArrayBufferView,
    exp: TypedArrayBufferView,
    tolerance: number | ((i: number) => number) = 0
  ): string | undefined {
    assert(actual.constructor === exp.constructor);

    const size = exp.byteLength;
    if (actual.byteLength !== size) {
      return 'size mismatch';
    }
    const failedByteIndices: string[] = [];
    const failedByteExpectedValues: string[] = [];
    const failedByteActualValues: string[] = [];
    for (let i = 0; i < size; ++i) {
      const tol = typeof tolerance === 'function' ? tolerance(i) : tolerance;
      if (Math.abs(actual[i] - exp[i]) > tol) {
        if (failedByteIndices.length >= 4) {
          failedByteIndices.push('...');
          failedByteExpectedValues.push('...');
          failedByteActualValues.push('...');
          break;
        }
        failedByteIndices.push(i.toString());
        failedByteExpectedValues.push(exp[i].toString());
        failedByteActualValues.push(actual[i].toString());
      }
    }
    const summary = `at [${failedByteIndices.join(', ')}], \
expected [${failedByteExpectedValues.join(', ')}], \
got [${failedByteActualValues.join(', ')}]`;
    const lines = [summary];

    // TODO: Could make a more convenient message, which could look like e.g.:
    //
    //   Starting at offset 48,
    //              got 22222222 ABCDABCD 99999999
    //     but expected 22222222 55555555 99999999
    //
    // or
    //
    //   Starting at offset 0,
    //              got 00000000 00000000 00000000 00000000 (... more)
    //     but expected 00FF00FF 00FF00FF 00FF00FF 00FF00FF (... more)
    //
    // Or, maybe these diffs aren't actually very useful (given we have the prints just above here),
    // and we should remove them. More important will be logging of texture data in a visual format.

    if (size <= 256 && failedByteIndices.length > 0) {
      const expHex = Array.from(new Uint8Array(exp.buffer, exp.byteOffset, exp.byteLength))
        .map(x => x.toString(16).padStart(2, '0'))
        .join('');
      const actHex = Array.from(new Uint8Array(actual.buffer, actual.byteOffset, actual.byteLength))
        .map(x => x.toString(16).padStart(2, '0'))
        .join('');
      lines.push('EXPECT:\t  ' + exp.join(' '));
      lines.push('\t0x' + expHex);
      lines.push('ACTUAL:\t  ' + actual.join(' '));
      lines.push('\t0x' + actHex);
    }
    if (failedByteIndices.length) {
      return lines.join('\n');
    }
    return undefined;
  }

  expectSingleColor(
    src: GPUTexture,
    format: GPUTextureFormat,
    {
      size,
      exp,
      dimension = '2d',
      slice = 0,
      layout,
    }: {
      size: [number, number, number];
      exp: PerTexelComponent<number>;
      dimension?: GPUTextureDimension;
      slice?: number;
      layout?: TextureLayoutOptions;
    }
  ): void {
    const { byteLength, bytesPerRow, rowsPerImage, mipSize } = getTextureCopyLayout(
      format,
      dimension,
      size,
      layout
    );
    const expectedTexelData = getTexelDataRepresentation(format).getBytes(exp);

    const buffer = this.device.createBuffer({
      size: byteLength,
      usage: GPUBufferUsage.COPY_SRC | GPUBufferUsage.COPY_DST,
    });

    const commandEncoder = this.device.createCommandEncoder();
    commandEncoder.copyTextureToBuffer(
      { texture: src, mipLevel: layout?.mipLevel, arrayLayer: slice },
      { buffer, bytesPerRow, rowsPerImage },
      mipSize
    );
    this.queue.submit([commandEncoder.finish()]);
    const arrayBuffer = new ArrayBuffer(byteLength);
    fillTextureDataWithTexelValue(expectedTexelData, format, dimension, arrayBuffer, size, layout);
    this.expectContents(buffer, new Uint8Array(arrayBuffer));
  }

  expectGPUError<R>(filter: GPUErrorFilter, fn: () => R): R {
    this.device.pushErrorScope(filter);
    const returnValue = fn();
    const promise = this.device.popErrorScope();

    this.eventualAsyncExpectation(async niceStack => {
      const error = await promise;

      let failed = false;
      switch (filter) {
        case 'none':
          failed = error !== null;
          break;
        case 'out-of-memory':
          failed = !(error instanceof GPUOutOfMemoryError);
          break;
        case 'validation':
          failed = !(error instanceof GPUValidationError);
          break;
      }

      if (failed) {
        niceStack.message = `Expected ${filter} error`;
        this.rec.expectationFailed(niceStack);
      } else {
        niceStack.message = `Captured ${filter} error`;
        if (error instanceof GPUValidationError) {
          niceStack.message += ` - ${error.message}`;
        }
        this.rec.debug(niceStack);
      }
    });

    return returnValue;
  }
}
