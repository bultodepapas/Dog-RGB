import type { Metadata } from "next";
import Link from "next/link";
import { AccountDeletionReceiptRecovery } from "../../components/account-deletion-receipt-recovery";

export const metadata: Metadata = { title: "Comprobante de eliminación | Dog RGB" };
export const dynamic = "force-dynamic";

export default function AccountDeletionReceiptPage() {
  return <main className="onboarding-content" id="main-content" tabIndex={-1}>
    <Link href="/">DOG-RGB_</Link>
    <AccountDeletionReceiptRecovery />
  </main>;
}
