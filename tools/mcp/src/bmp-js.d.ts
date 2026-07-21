declare module "bmp-js" {
  export interface BmpDecoded {
    width: number;
    height: number;
    /** 픽셀당 4바이트, ABGR 순서. */
    data: Buffer;
  }
  export function decode(buffer: Buffer): BmpDecoded;
  export function encode(image: { data: Buffer; width: number; height: number }, quality?: number): { data: Buffer };
}
