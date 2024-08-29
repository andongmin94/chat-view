import React from 'react'
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Switch } from "@/components/ui/switch"
import { CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card"
import { Dialog, DialogContent, DialogDescription, DialogFooter, DialogHeader, DialogTitle, DialogTrigger } from "@/components/ui/dialog"
import { z } from "zod"

const urlSchema = z.string().url().startsWith("http://afreehp.kr/page/")

// 실제 Electron 애플리케이션에서는 이 부분을 Electron의 IPC를 통해 구현해야 합니다.
const electronStore = {
  get: (key: string) => localStorage.getItem(key),
  set: (key: string, value: string) => localStorage.setItem(key, value),
}

export default function Component() {
  const [url, setUrl] = React.useState('')
  const [isFixed, setIsFixed] = React.useState(false)
  const [isDialogOpen, setIsDialogOpen] = React.useState(false)
  const [urlError, setUrlError] = React.useState<string | null>(null)
  const [isFirstRun, setIsFirstRun] = React.useState(true)

  React.useEffect(() => {
    const savedUrl = electronStore.get('chatUrl')
    if (savedUrl) {
      setUrl(savedUrl)
      setIsFirstRun(false)
    } else {
      setIsDialogOpen(true)
    }
  }, [])

  const handleUrlChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const newUrl = e.target.value
    setUrl(newUrl)
    try {
      urlSchema.parse(newUrl)
      setUrlError(null)
    } catch (error) {
      if (error instanceof z.ZodError) {
        setUrlError("http://afreehp.kr/page/로 시작해야 합니다.");
      }
    }
  }

  const handleFixedToggle = (checked: boolean) => {
    setIsFixed(checked)
    // Here you would typically call electron IPC to update overlay window settings
    console.log("고정 활성화:", checked)
  }

  const handleApply = () => {
    if (!urlError) {
      setIsDialogOpen(false)
      electronStore.set('chatUrl', url)
      setIsFirstRun(false)
      // Here you would typically call electron IPC to update the URL
      console.log("URL 적용:", url)
    }
  }

  return (
    <div className="h-[300px]">
      <CardHeader className='flex justify-center'>
        <CardTitle>채팅 오버레이 제어판</CardTitle>
        <CardDescription>채팅 오버레이 설정을 관리합니다</CardDescription>
      </CardHeader>
      <CardContent>
        <div className="grid w-full items-center gap-4">
          <Dialog open={isDialogOpen} onOpenChange={setIsDialogOpen}>
            <DialogTrigger asChild>
              <Button variant="outline">
                {isFirstRun ? "URL 입력하기" : "URL 다시 입력하기"}
              </Button>
            </DialogTrigger>
            <DialogContent className="max-w-[355px] rounded-md">
              <DialogHeader>
                <DialogTitle>채팅 URL 설정</DialogTitle>
                <DialogDescription>
                  채팅 오버레이에 표시할 URL을 입력하세요.
                </DialogDescription>
              </DialogHeader>
              <div className="grid gap-4 py-4">
                <div className="grid grid-cols-1 items-center gap-4">
                  <Input
                    id="url"
                    value={url}
                    onChange={handleUrlChange}
                    className="col-span-2"
                  />
                </div>
                {urlError && (
                  <p className="text-sm text-red-500">{urlError}</p>
                )}
              </div>
              <DialogFooter>
                <Button onClick={handleApply} disabled={!!urlError}>적용</Button>
              </DialogFooter>
            </DialogContent>
          </Dialog>
          <div className="flex items-center justify-between">
            <Label htmlFor="fixed-mode">고정 활성화</Label>
            <Switch
              id="fixed-mode"
              checked={isFixed}
              onCheckedChange={handleFixedToggle}
            />
          </div>
        </div>
      </CardContent>
    </div>
  )
}