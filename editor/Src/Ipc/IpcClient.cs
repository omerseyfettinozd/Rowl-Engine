using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Ipc
{
    public enum MessageType : ushort
    {
        HandshakeReq = 0,
        HandshakeResp = 1,
        UpdateNodeGraph = 2,
        SetActiveNode = 3,
        UpdateVariable = 4,
        TriggerEvent = 5,
        Heartbeat = 6
    }

    public class IpcClient
    {
        private Socket? _socket;
        private readonly string _pipeName;
        public bool IsConnected => _socket != null && _socket.Connected;

        public IpcClient(string pipeName = "rowl_engine_ipc")
        {
            _pipeName = pipeName;
        }

        public async Task<bool> ConnectAsync()
        {
            try
            {
                string socketPath = $"/tmp/{_pipeName}.sock";
                var endPoint = new UnixDomainSocketEndPoint(socketPath);

                _socket = new Socket(AddressFamily.Unix, SocketType.Stream, ProtocolType.Unspecified);
                await _socket.ConnectAsync(endPoint);

                return true;
            }
            catch
            {
                _socket = null;
                return false;
            }
        }

        public async Task<bool> SendPacketAsync(MessageType type, byte[] payload)
        {
            if (_socket == null || !_socket.Connected)
            {
                bool reconnected = await ConnectAsync();
                if (!reconnected) return false;
            }

            try
            {
                byte[] magic = Encoding.ASCII.GetBytes("ROWL");
                byte[] typeBytes = BitConverter.GetBytes((ushort)type);
                byte[] sizeBytes = BitConverter.GetBytes((uint)payload.Length);

                // Fixed: 12-byte header to match C++ server (4 magic + 2 type + 2 reserved + 4 size)
                byte[] header = new byte[12];
                Buffer.BlockCopy(magic, 0, header, 0, 4);
                Buffer.BlockCopy(typeBytes, 0, header, 4, 2);
                // 2 bytes reserved at offset 6-7 (zero-initialized)
                Buffer.BlockCopy(sizeBytes, 0, header, 8, 4);

                await _socket!.SendAsync(header, SocketFlags.None);
                if (payload.Length > 0)
                {
                    await _socket.SendAsync(payload, SocketFlags.None);
                }

                return true;
            }
            catch
            {
                Disconnect();
                return false;
            }
        }

        public void Disconnect()
        {
            if (_socket != null)
            {
                try
                {
                    _socket.Shutdown(SocketShutdown.Both);
                    _socket.Close();
                }
                catch { }
                _socket = null;
            }
        }
    }
}
