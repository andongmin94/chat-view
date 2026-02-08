import { z } from "zod";

export const URL_REQUIRED_ERROR = "URL을 입력하세요.";
export const URL_PREFIX_ERROR =
  "https://weflab.com/page/ 또는 https://chzzk.naver.com/chat/ 로 시작해야 합니다.";

export const chatUrlSchema = z
  .string()
  .url()
  .refine((url) => {
    return (
      url.startsWith("https://weflab.com/page/") ||
      url.startsWith("https://chzzk.naver.com/chat/")
    );
  });
