interface ControllerFooterProps {
  version: string;
}

export default function ControllerFooter({ version }: ControllerFooterProps) {
  return (
    <div className="pointer-events-none mr-1 flex justify-end text-xs">
      v{version}
    </div>
  );
}
