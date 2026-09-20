#include "ai.h"
#include "http.h"

extern void print(const char *text);

#define AI_HISTORY_SIZE 16
#define AI_TEXT_SIZE 128
#define AI_RESPONSE_SIZE 2048

static int ai_initialized = 0;

static char ai_user_history[AI_HISTORY_SIZE][AI_TEXT_SIZE];
static char ai_answer_history[AI_HISTORY_SIZE][AI_RESPONSE_SIZE];

static int ai_history_count = 0;

static void ai_copy(
    char *dst,
    const char *src,
    unsigned int capacity)
{
    unsigned int i = 0;

    if (dst == 0 || src == 0 || capacity == 0)
        return;

    while (src[i] != '\0' &&
           i < capacity - 1)
    {
        dst[i] = src[i];
        i++;
    }

    dst[i] = '\0';
}

static void ai_store(
    const char *user,
    const char *answer)
{
    int i;

    if (ai_history_count < AI_HISTORY_SIZE)
    {
        ai_copy(
            ai_user_history[ai_history_count],
            user,
            AI_TEXT_SIZE);

        ai_copy(
            ai_answer_history[ai_history_count],
            answer,
            AI_RESPONSE_SIZE);

        ai_history_count++;

        return;
    }

    for (i = 1; i < AI_HISTORY_SIZE; i++)
    {
        ai_copy(
            ai_user_history[i - 1],
            ai_user_history[i],
            AI_TEXT_SIZE);

        ai_copy(
            ai_answer_history[i - 1],
            ai_answer_history[i],
            AI_RESPONSE_SIZE);
    }

    ai_copy(
        ai_user_history[AI_HISTORY_SIZE - 1],
        user,
        AI_TEXT_SIZE);

    ai_copy(
        ai_answer_history[AI_HISTORY_SIZE - 1],
        answer,
        AI_RESPONSE_SIZE);
}

void ai_init(void)
{
    ai_initialized = 1;
    ai_history_count = 0;
}

void ai_print_status(void)
{
    print("\nAnnabelle.AI\n");
    print("------------------------------\n");

    print("Status       : ");

    if (ai_initialized)
        print("Initialized\n");
    else
        print("Not initialized\n");

    print("Backend      : Connected\n");
    print("Network      : HTTP/TCP\n");
    print("Model        : Backend\n");

    print("Conversation : ");

    if (ai_history_count == 0)
        print("Empty\n");
    else
        print("Active\n");

    print("------------------------------\n\n");
}

void ai_print_history(void)
{
    int i;

    print("\nAnnabelle.AI dialogue history\n");
    print("=============================\n");

    if (ai_history_count == 0)
    {
        print("No Annabelle.AI dialogue yet.\n\n");
        return;
    }

    for (i = 0; i < ai_history_count; i++)
    {
        print("You: ");
        print(ai_user_history[i]);

        print("\nAnnabelle.AI: ");
        print(ai_answer_history[i]);

        print("\n\n");
    }
}

void ai_prompt(char *input)
{
    unsigned char backend_ip[4];

    char json_body[256];
    char response[AI_RESPONSE_SIZE];

    unsigned int i;
    unsigned int position;

    if (!ai_initialized)
    {
        print("\nAnnabelle.AI: subsystem is not initialized.\n\n");
        return;
    }

    if (input == 0 || input[0] == '\0')
    {
        print("\nAnnabelle.AI\n");
        print("------------------------------\n");
        print("Usage: prompt <message>\n");
        print("Example: prompt hello\n");
        print("------------------------------\n\n");

        return;
    }

    /*
     * Annabelle.AI backend running inside WSL.
     * Current WSL address: 172.30.196.50
     */
    /*
     * QEMU user networking:
     * 10.0.2.2 = host machine.
     * Windows forwards port 8000 to the WSL backend.
     */
    backend_ip[0] = 10;
    backend_ip[1] = 0;
    backend_ip[2] = 2;
    backend_ip[3] = 2;

    position = 0;

    /*
     * Build:
     *
     * {"message":"USER INPUT"}
     */
    json_body[position++] = '{';
    json_body[position++] = '"';
    json_body[position++] = 'm';
    json_body[position++] = 'e';
    json_body[position++] = 's';
    json_body[position++] = 's';
    json_body[position++] = 'a';
    json_body[position++] = 'g';
    json_body[position++] = 'e';
    json_body[position++] = '"';
    json_body[position++] = ':';
    json_body[position++] = '"';

    i = 0;

    while (input[i] != '\0' &&
           position < sizeof(json_body) - 3)
    {
        /*
         * Escape JSON quote and backslash.
         */
        if (input[i] == '"' ||
            input[i] == '\\')
        {
            json_body[position++] = '\\';
        }

        json_body[position++] =
            (unsigned char)input[i];

        i++;
    }

    json_body[position++] = '"';
    json_body[position++] = '}';
    json_body[position] = '\0';

    response[0] = '\0';

    print("\n");
    print("Annabelle.AI\n");
    print("------------------------------\n");

    print("You: ");
    print(input);
    print("\n\n");

    if (!http_post(
            backend_ip,
            8000,
            "10.0.2.2",
            "/chat",
            json_body,
            response,
            sizeof(response)))
    {
        print("\nAnnabelle.AI: backend connection failed.\n");
        print("------------------------------\n\n");
        return;
    }

    print("\nAnnabelle.AI: ");

    if (response[0] == '\0')
    {
        print("Backend returned an empty response.\n");
    }
    else
    {
        print(response);
        print("\n");
    }

    ai_store(
        input,
        response);

    print("------------------------------\n\n");
}
