const CONFIRMATION_SUBJECT = "Confirma tu cuenta Dog RGB";

function mailpitOrigin() {
  const value = process.env.M113_MAILPIT_URL;
  let url;
  try {
    url = new URL(value);
  } catch {
    throw new Error("M1.13 Mailpit URL is invalid.");
  }
  if (url.protocol !== "http:" || url.hostname !== "127.0.0.1" || url.pathname !== "/") {
    throw new Error("M1.13 Mailpit must be the loopback local service.");
  }
  return url.origin;
}

async function json(path, init) {
  const response = await fetch(`${mailpitOrigin()}${path}`, {
    ...init,
    signal: AbortSignal.timeout(2_000),
  });
  if (!response.ok) throw new Error("M1.13 Mailpit request failed.");
  if (response.status === 204) return null;
  const body = await response.text();
  try {
    return JSON.parse(body);
  } catch {
    return body;
  }
}

function messagesFrom(value) {
  if (Array.isArray(value?.messages)) return value.messages;
  if (Array.isArray(value?.Messages)) return value.Messages;
  return [];
}

function recipients(message) {
  const values = Array.isArray(message?.To) ? message.To : [];
  return values.map((recipient) => recipient?.Address).filter(Boolean);
}

export async function clearMailbox() {
  await json("/api/v1/messages", { method: "DELETE" });
  const mailbox = await json("/api/v1/messages?start=0&limit=50");
  if (messagesFrom(mailbox).length !== 0) {
    throw new Error("M1.13 Mailpit cleanup did not converge.");
  }
}

export async function takeConfirmationLink(email, timeoutMs = 15_000) {
  const deadline = Date.now() + timeoutMs;
  let match = null;
  while (Date.now() < deadline) {
    const mailbox = await json("/api/v1/messages?start=0&limit=50");
    const matches = messagesFrom(mailbox).filter((message) =>
      message?.Subject === CONFIRMATION_SUBJECT && recipients(message).includes(email));
    if (matches.length > 1) {
      throw new Error("M1.13 Mailpit contained duplicate confirmation messages.");
    }
    if (matches.length === 1) {
      match = matches[0];
      break;
    }
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  if (!match) throw new Error("M1.13 confirmation message did not arrive before its deadline.");
  const messageId = match.ID ?? match.Id ?? match.id;
  if (typeof messageId !== "string" || messageId.length < 1 || messageId.length > 256) {
    throw new Error("M1.13 Mailpit returned an invalid message identity.");
  }
  const message = await json(`/api/v1/message/${encodeURIComponent(messageId)}`);
  const content = [message?.HTML, message?.Text]
    .filter((value) => typeof value === "string")
    .join("\n")
    .replaceAll("&amp;", "&");
  const candidates = content.match(/https?:\/\/[^\s"'<>]+/gu) ?? [];
  const links = [...new Map(candidates.flatMap((candidate) => {
    try {
      const url = new URL(candidate);
      return url.origin === "http://127.0.0.1:3000" &&
          url.pathname === "/auth/confirm"
        ? [[url.toString(), url]]
        : [];
    } catch {
      return [];
    }
  })).values()];
  if (links.length !== 1) throw new Error("M1.13 confirmation link shape was invalid.");
  const link = links[0];
  const keys = [...link.searchParams.keys()].sort();
  const tokenHash = link.searchParams.get("token_hash");
  if (
    keys.join(",") !== "token_hash,type" ||
    link.searchParams.get("type") !== "email" ||
    typeof tokenHash !== "string" ||
    tokenHash.length < 32 || tokenHash.length > 256
  ) {
    throw new Error("M1.13 confirmation link parameters were invalid.");
  }
  await clearMailbox();
  return Object.freeze({ url: link.toString(), tokenHash });
}
