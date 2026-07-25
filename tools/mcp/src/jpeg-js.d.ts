declare module "jpeg-js" {
  export interface JpegDecoded {
    width: number;
    height: number;
    /** 픽셀당 4바이트, RGBA 순서. */
    data: Buffer;
  }
  export function decode(buffer: Buffer, opts?: { formatAsRGBA?: boolean }): JpegDecoded;
  export function encode(image: { data: Buffer; width: number; height: number }, quality?: number): { data: Buffer };
}
