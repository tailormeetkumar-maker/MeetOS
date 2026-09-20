#!/usr/bin/env python3

import json
import os
import re
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, HTTPServer

# ============================================================
# Annabelle.AI - Local AI Backend
# ============================================================

HOST = "0.0.0.0"
PORT = 8000

OLLAMA_URL = "http://127.0.0.1:11434/api/chat"
OLLAMA_MODEL = "qwen3:4b-instruct"

MAX_CONVERSATION = 20
MAX_CONTEXT_MESSAGES = 12

conversation = []
MEMORY = {}


# ============================================================
# Utility
# ============================================================

def normalize(text):
    return " ".join(text.lower().strip().split())


def contains_any(text, words):
    text = normalize(text)

    for word in words:
        if word in text:
            return True

    return False


def remember_conversation(user_message, answer):
    conversation.append({
        "user": user_message,
        "assistant": answer
    })

    if len(conversation) > MAX_CONVERSATION:
        conversation.pop(0)


# ============================================================
# Intent detection
# ============================================================

def detect_intent(message):
    text = normalize(message)

    if contains_any(text, [
        "hello", "hi", "hey", "good morning",
        "good afternoon", "good evening"
    ]):
        return "greeting"

    if contains_any(text, [
        "who are you", "what are you", "your name",
        "tell me about yourself"
    ]):
        return "identity"

    if contains_any(text, [
        "how are you", "are you okay", "how do you feel"
    ]):
        return "wellbeing"

    if contains_any(text, [
        "thank you", "thanks", "thank"
    ]):
        return "thanks"

    if contains_any(text, [
        "bye", "goodbye", "see you"
    ]):
        return "goodbye"

    if contains_any(text, [
        "remember", "remember that"
    ]):
        return "memory"

    if contains_any(text, [
        "what did i say", "do you remember",
        "what do you remember"
    ]):
        return "recall"

    if re.search(r"\d+\s*[\+\-\*\/]\s*\d+", text):
        return "calculation"

    return "general"


# ============================================================
# Memory
# ============================================================

def process_memory(message):
    text = message.strip()

    lowered = text.lower()

    if lowered.startswith("remember that "):
        value = text[14:].strip()

        if value:
            MEMORY["user_note"] = value
            return "I'll remember that for this conversation."

    if lowered.startswith("remember "):
        value = text[9:].strip()

        if value:
            MEMORY["user_note"] = value
            return "I'll remember that for this conversation."

    return None


def recall_memory():
    if "user_note" not in MEMORY:
        return "I don't have any saved personal notes in this conversation yet."

    return "You asked me to remember: " + MEMORY["user_note"]


# ============================================================
# Local LLM
# ============================================================

def build_system_prompt():
    memory_text = ""

    if MEMORY:
        memory_text = "\nConversation memory:\n"

        for key, value in MEMORY.items():
            memory_text += "- " + key + ": " + value + "\n"

    return (
        "You are Annabelle.AI, the local AI assistant integrated into "
        "the MeetOS educational operating system.\n\n"

        "Your name is Annabelle.AI.\n"
        "You are helpful, intelligent, calm, and conversational.\n"
        "You should answer naturally rather than sounding like a "
        "fixed rule-based chatbot.\n\n"

        "MeetOS is an educational operating system being built from "
        "scratch. It has a custom kernel, shell, VGA terminal, keyboard "
        "input, RTL8139 networking, Ethernet, ARP, IPv4, UDP, DNS, TCP "
        "and HTTP networking.\n\n"

        "You are running completely locally through Ollama. "
        "Do not claim that you are connected to OpenAI or another "
        "external AI service.\n\n"

        "Answer the user's actual question directly. "
        "If the user asks a technical question, explain it clearly. "
        "If the user asks for code, provide useful code and explain "
        "important parts.\n\n"

        "Use the previous conversation when it is relevant. "
        "Do not repeat the user's question unnecessarily.\n"

        "Keep normal answers reasonably concise, but provide more "
        "detail when the question requires it."
        + memory_text
    )


def generate_answer(message):
    messages = [
        {
            "role": "system",
            "content": build_system_prompt()
        }
    ]

    # Give the model recent conversation context.
    recent = conversation[-MAX_CONTEXT_MESSAGES:]

    for item in recent:
        messages.append({
            "role": "user",
            "content": item["user"]
        })

        messages.append({
            "role": "assistant",
            "content": item["assistant"]
        })

    messages.append({
        "role": "user",
        "content": message
    })

    payload = {
        "model": OLLAMA_MODEL,
        "messages": messages,
        "stream": False,
        "options": {
            "temperature": 0.7
        }
    }

    data = json.dumps(payload).encode("utf-8")

    request = urllib.request.Request(
        OLLAMA_URL,
        data=data,
        headers={
            "Content-Type": "application/json"
        },
        method="POST"
    )

    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            raw = response.read().decode("utf-8")

        result = json.loads(raw)

        message_object = result.get("message", {})
        answer = message_object.get("content", "")

        if not answer:
            return "Annabelle.AI: The local model returned an empty response."

        return answer.strip()

    except urllib.error.URLError as error:
        print("Ollama connection error:", error)
        return (
            "Annabelle.AI: I cannot reach the local AI engine. "
            "Please make sure Ollama is running."
        )

    except Exception as error:
        print("Local AI error:", error)
        return "Annabelle.AI: The local AI engine encountered an error."


# ============================================================
# HTTP server
# ============================================================

class AnnabelleHandler(BaseHTTPRequestHandler):

    def send_text(self, status_code, text):
        body = text.encode("utf-8")

        self.send_response(status_code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):

        if self.path != "/chat":
            self.send_text(404, "Not found")
            return

        try:
            content_length = int(
                self.headers.get("Content-Length", "0")
            )

            body = self.rfile.read(content_length)

            request_data = json.loads(
                body.decode("utf-8")
            )

            user_message = request_data.get("message", "")

            if not isinstance(user_message, str):
                user_message = str(user_message)

            user_message = user_message.strip()

            print()
            print("--------------------------------------------------")
            print("MeetOS request:")
            print(user_message)
            print("Intent:", detect_intent(user_message))
            print("--------------------------------------------------")

            if not user_message:
                self.send_text(
                    400,
                    "Annabelle.AI: Empty message."
                )
                return

            # Handle explicit memory commands locally.
            memory_answer = process_memory(user_message)

            if memory_answer is not None:
                answer = memory_answer

            elif detect_intent(user_message) == "recall":
                answer = recall_memory()

            else:
                answer = generate_answer(user_message)

            remember_conversation(
                user_message,
                answer
            )

            print("Annabelle.AI:")
            print(answer)
            print("--------------------------------------------------")

            self.send_text(
                200,
                answer
            )

        except Exception as error:
            print("HTTP handler error:", error)

            self.send_text(
                500,
                "Annabelle.AI: Backend error."
            )

    def log_message(self, format, *args):
        print("[HTTP]", format % args)


# ============================================================
# Main
# ============================================================

def main():

    print()
    print("==================================================")
    print("              Annabelle.AI Backend")
    print("==================================================")
    print("Version : 2.0")
    print("Mode    : LOCAL AI")
    print()
    print("AI Engine : Ollama")
    print("Model     :", OLLAMA_MODEL)
    print("Endpoint  :", OLLAMA_URL)
    print()
    print("Components:")
    print("  - Local language model")
    print("  - Conversation memory")
    print("  - Conversation context")
    print("  - Intent detection")
    print("  - MeetOS HTTP interface")
    print()
    print("No OpenAI API required.")
    print("No external AI credits required.")
    print("==================================================")
    print()

    server = HTTPServer(
        (HOST, PORT),
        AnnabelleHandler
    )

    print("Listening on 0.0.0.0:8000")
    print("Waiting for MeetOS...")
    print()

    try:
        server.serve_forever()

    except KeyboardInterrupt:
        print()
        print("Stopping Annabelle.AI backend...")

    finally:
        server.server_close()


if __name__ == "__main__":
    main()
