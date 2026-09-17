import './globals.css';

export const metadata = { title: 'XIAO Cam Log', description: 'Live camera capture and session browser' };

export default function RootLayout({ children }) {
  return (
    <html lang="en" suppressHydrationWarning>
      <body suppressHydrationWarning>{children}</body>
    </html>
  );
}