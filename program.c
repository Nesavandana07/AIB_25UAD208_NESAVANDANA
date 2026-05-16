#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8085
#define BUFFER_SIZE 4096
#define MAX_ACCOUNTS 100
#define MIN_BALANCE 100.0

// Data Structure Requirements
typedef struct {
    int account_number;
    char name[50];
    double balance;
    char status[10];
} Account;

Account accounts[MAX_ACCOUNTS];
int account_count = 0;

// Internal Logic Functions
void loadAccounts() {
    FILE *file = fopen("accounts.txt", "r");
    if (!file) return;

    account_count = 0;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), file) != NULL && account_count < MAX_ACCOUNTS) {
        if (buffer[0] == '\n' || buffer[0] == '\r') continue;
        int acc_num;
        char temp_name[50];
        double bal;
        char stat[10];
        if (sscanf(buffer, "%d %49s %lf %9s", &acc_num, temp_name, &bal, stat) == 4) {
            accounts[account_count].account_number = acc_num;
            strncpy(accounts[account_count].name, temp_name, 49);
            accounts[account_count].balance = bal;
            strncpy(accounts[account_count].status, stat, 9);
            account_count++;
        }
    }
    fclose(file);
    printf("Loaded %d accounts from accounts.txt\n", account_count);
}

void saveAccounts() {
    FILE *file = fopen("accounts.txt", "w");
    if (!file) return;
    for (int i = 0; i < account_count; i++) {
        fprintf(file, "%d %s %.2f %s\n", 
                accounts[i].account_number, 
                accounts[i].name, 
                accounts[i].balance, 
                accounts[i].status);
    }
    fclose(file);
}

int findAccountIndex(int account_number) {
    for (int i = 0; i < account_count; i++) {
        if (accounts[i].account_number == account_number) return i;
    }
    return -1;
}

// HTTP Response Helpers
void sendHttpResponse(SOCKET client_socket, const char *status, const char *content_type, const char *body) {
    char headers[1024];
    int body_len = strlen(body);
    sprintf(headers,
            "HTTP/1.1 %s\r\n"
            "Content-Type: %s\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Pragma: no-cache\r\n"
            "Expires: 0\r\n"
            "Connection: close\r\n"
            "Content-Length: %d\r\n\r\n",
            status, content_type, body_len);
    send(client_socket, headers, strlen(headers), 0);
    if (body_len > 0) {
        send(client_socket, body, body_len, 0);
    }
}

void serveFile(SOCKET client_socket, const char *filepath) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        sendHttpResponse(client_socket, "404 Not Found", "text/html", "<h1>404 File Not Found</h1>");
        return;
    }
    fseek(file, 0, SEEK_END);
    long fsize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *file_buffer = malloc(fsize + 1);
    fread(file_buffer, 1, fsize, file);
    fclose(file);
    file_buffer[fsize] = 0;

    sendHttpResponse(client_socket, "200 OK", "text/html", file_buffer);
    free(file_buffer);
}

void sendJsonSuccess(SOCKET client_socket, const char *message) {
    char json[256];
    sprintf(json, "{\"success\": true, \"message\": \"%s\"}", message);
    sendHttpResponse(client_socket, "200 OK", "application/json", json);
}

void sendJsonError(SOCKET client_socket, const char *message) {
    char json[256];
    sprintf(json, "{\"success\": false, \"message\": \"%s\"}", message);
    sendHttpResponse(client_socket, "400 Bad Request", "application/json", json);
}

// Simple JSON extraction helper
void extractJsonValueStr(const char *json, const char *key, char *out, int max_len) {
    char search_key[100];
    sprintf(search_key, "\"%s\":\"", key);
    char *ptr = strstr(json, search_key);
    if (!ptr) { out[0] = '\0'; return; }
    ptr += strlen(search_key);
    int i = 0;
    while (*ptr != '"' && *ptr != '\0' && i < max_len - 1) {
        out[i++] = *ptr++;
    }
    out[i] = '\0';
}

double extractJsonValueNum(const char *json, const char *key) {
    char search_key[100];
    sprintf(search_key, "\"%s\":", key);
    char *ptr = strstr(json, search_key);
    if (!ptr) return 0.0;
    ptr += strlen(search_key);
    return atof(ptr);
}

// API Route Handlers
void handleGetAccounts(SOCKET client_socket) {
    char json[BUFFER_SIZE * 4] = "{\"accounts\": [";
    for (int i = 0; i < account_count; i++) {
        char temp[256];
        sprintf(temp, "{\"account_number\":%d,\"name\":\"%s\",\"balance\":%.2f,\"status\":\"%s\"}%s",
                accounts[i].account_number, accounts[i].name, accounts[i].balance, accounts[i].status,
                (i == account_count - 1) ? "" : ",");
        strcat(json, temp);
    }
    strcat(json, "]}");
    sendHttpResponse(client_socket, "200 OK", "application/json", json);
}

void handlePostAccount(SOCKET client_socket, const char *body) {
    if (account_count >= MAX_ACCOUNTS) {
        sendJsonError(client_socket, "Maximum account limit reached");
        return;
    }

    int acc_num = (int)extractJsonValueNum(body, "account_number");
    char name[50];
    extractJsonValueStr(body, "name", name, 50);
    double balance = extractJsonValueNum(body, "balance");

    if (acc_num <= 0 || strlen(name) == 0 || balance < 0) {
        sendJsonError(client_socket, "Invalid input data");
        return;
    }

    if (findAccountIndex(acc_num) != -1) {
        sendJsonError(client_socket, "Account number already exists");
        return;
    }

    Account newAcc;
    newAcc.account_number = acc_num;
    strncpy(newAcc.name, name, 49);
    newAcc.name[49] = '\0';
    newAcc.balance = balance;
    strcpy(newAcc.status, "Active");

    accounts[account_count++] = newAcc;
    saveAccounts();

    sendJsonSuccess(client_socket, "Account created successfully!");
}

void handlePostTransaction(SOCKET client_socket, const char *body) {
    int acc_num = (int)extractJsonValueNum(body, "account_number");
    int type = (int)extractJsonValueNum(body, "type");
    double amount = extractJsonValueNum(body, "amount");

    int idx = findAccountIndex(acc_num);
    if (idx == -1) {
        sendJsonError(client_socket, "Account not found");
        return;
    }

    if (strcmp(accounts[idx].status, "Inactive") == 0) {
        sendJsonError(client_socket, "Account is inactive. Transactions blocked.");
        return;
    }

    if (amount <= 0) {
        sendJsonError(client_socket, "Amount must be greater than 0");
        return;
    }

    if (type == 1) { // Deposit
        accounts[idx].balance += amount;
    } else if (type == 2) { // Withdraw
        if (amount > accounts[idx].balance) {
            sendJsonError(client_socket, "Insufficient funds");
            return;
        }
        if (accounts[idx].balance - amount < MIN_BALANCE) {
            char msg[100];
            sprintf(msg, "Withdrawal drops balance below min %.2f", MIN_BALANCE);
            sendJsonError(client_socket, msg);
            return;
        }
        accounts[idx].balance -= amount;
    } else {
        sendJsonError(client_socket, "Invalid transaction type");
        return;
    }

    saveAccounts();
    sendJsonSuccess(client_socket, "Transaction completed successfully!");
}

int main() {
    // Load initial data
    loadAccounts();

    FILE *log = fopen("server_log.txt", "w");
    if (log) { fprintf(log, "Server started\n"); fflush(log); }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        if (log) { fprintf(log, "WSAStartup failed.\n"); fclose(log); }
        return 1;
    }

    SOCKET server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    int client_addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) {
        if (log) { fprintf(log, "Socket creation failed. Error: %d\n", WSAGetLastError()); fclose(log); }
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        if (log) { fprintf(log, "Bind failed. Error: %d\n", WSAGetLastError()); fclose(log); }
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, 10) == SOCKET_ERROR) {
        if (log) { fprintf(log, "Listen failed. Error: %d\n", WSAGetLastError()); fclose(log); }
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (log) { fprintf(log, "Listening on port %d\n", PORT); fflush(log); }

    printf("==========================================\n");
    printf(" Transaction Web Server running in C!\n");
    printf(" Open your browser to http://localhost:%d\n", PORT);
    printf("==========================================\n");

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) continue;

        char buffer[BUFFER_SIZE * 2] = {0};
        int bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read > 0) {
            FILE *log = fopen("server_log.txt", "a");
            if (log) { fprintf(log, "--- REQUEST ---\n%s\n", buffer); fclose(log); }

            // Find body if it's a POST
            char *body = strstr(buffer, "\r\n\r\n");
            if (body) {
                body += 4;
                char *cl_ptr = strstr(buffer, "Content-Length: ");
                if (cl_ptr) {
                    int content_length = atoi(cl_ptr + 16);
                    int body_received = bytes_read - (body - buffer);
                    while (body_received < content_length && bytes_read < (BUFFER_SIZE * 2) - 1) {
                        int more = recv(client_socket, buffer + bytes_read, (BUFFER_SIZE * 2) - 1 - bytes_read, 0);
                        if (more <= 0) break;
                        bytes_read += more;
                        body_received += more;
                    }
                }
            }

            // Route matching
            if (strncmp(buffer, "GET / ", 6) == 0) {
                serveFile(client_socket, "index.html");
            } else if (strncmp(buffer, "GET /api/accounts", 17) == 0) {
                handleGetAccounts(client_socket);
            } else if (strncmp(buffer, "POST /api/accounts", 18) == 0) {
                handlePostAccount(client_socket, body);
            } else if (strncmp(buffer, "POST /api/transaction", 21) == 0) {
                handlePostTransaction(client_socket, body);
            } else {
                sendHttpResponse(client_socket, "404 Not Found", "text/plain", "Route not found");
            }
        }

        closesocket(client_socket);
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
