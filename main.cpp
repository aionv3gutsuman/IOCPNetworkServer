#include <winsock2.h>
#include <mswsock.h>
#include <windows.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

// I/O データ構造
struct PER_IO_DATA {
	WSAOVERLAPPED overlapped;
	WSABUF buffer;
	char data[1024];
	DWORD bytes;
	int operation; // 0=recv, 1=send, 2=accept
	SOCKET sock;
};

// AcceptEx 用の関数ポインタ
LPFN_ACCEPTEX lpAcceptEx;

void PostAccept(SOCKET listenSock, HANDLE iocp) {
	SOCKET client = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	PER_IO_DATA* io = new PER_IO_DATA();
	ZeroMemory(&io->overlapped, sizeof(io->overlapped));
	io->operation = 2;
	io->sock = client;

	DWORD bytes = 0;
	lpAcceptEx(
		listenSock,
		client,
		io->data,
		0,
		sizeof(sockaddr_in)+16,
		sizeof(sockaddr_in)+16,
		&bytes,
		&io->overlapped
		);
}

int main() {
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	SOCKET listenSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(5000);

	bind(listenSock, (sockaddr*)&addr, sizeof(addr));
	listen(listenSock, SOMAXCONN);

	// AcceptEx を取得
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	DWORD bytes = 0;
	WSAIoctl(
		listenSock,
		SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx),
		&lpAcceptEx, sizeof(lpAcceptEx),
		&bytes, NULL, NULL
		);

	// IOCP 作成
	HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);

	// listen ソケットを IOCP に関連付け
	CreateIoCompletionPort((HANDLE)listenSock, iocp, (ULONG_PTR)listenSock, 0);

	// 最初の accept を投げる
	PostAccept(listenSock, iocp);

	printf("IOCP server started\n");

	while (true) {
		DWORD bytes;
		ULONG_PTR key;
		PER_IO_DATA* io;

		BOOL ok = GetQueuedCompletionStatus(
			iocp, &bytes, &key, (LPOVERLAPPED*)&io, INFINITE);

		if (!ok) {
			printf("Error or disconnect\n");
			continue;
		}

		if (io->operation == 2) {
			// Accept 完了
			printf("Client connected\n");

			// クライアントソケットを IOCP に関連付け
			CreateIoCompletionPort((HANDLE)io->sock, iocp, (ULONG_PTR)io->sock, 0);

			// 次の accept を投げる
			PostAccept(listenSock, iocp);

			// このクライアントの最初の recv を投げる
			ZeroMemory(&io->overlapped, sizeof(io->overlapped));
			io->buffer.buf = io->data;
			io->buffer.len = sizeof(io->data);
			io->operation = 0;

			DWORD flags = 0;
			WSARecv(io->sock, &io->buffer, 1, NULL, &flags, &io->overlapped, NULL);
		}
		else if (io->operation == 0) {
			// recv 完了
			if (bytes == 0) {
				printf("Client disconnected\n");
				closesocket(io->sock);
				delete io;
				continue;
			}

			printf("Received: %.*s\n", bytes, io->data);

			// 受信データをそのまま送り返す
			ZeroMemory(&io->overlapped, sizeof(io->overlapped));
			io->buffer.len = bytes;
			io->operation = 1;

			WSASend(io->sock, &io->buffer, 1, NULL, 0, &io->overlapped, NULL);
		}
		else if (io->operation == 1) {
			// send 完了 → 次の recv を投げる
			ZeroMemory(&io->overlapped, sizeof(io->overlapped));
			io->buffer.buf = io->data;
			io->buffer.len = sizeof(io->data);
			io->operation = 0;

			DWORD flags = 0;
			WSARecv(io->sock, &io->buffer, 1, NULL, &flags, &io->overlapped, NULL);
		}
	}

	WSACleanup();
	return 0;
}