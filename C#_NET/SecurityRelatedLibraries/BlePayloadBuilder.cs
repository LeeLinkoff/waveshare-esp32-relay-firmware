using System;
using System.Linq;
using System.Security.Cryptography;
using System.Text;


namespace SecurityRelatedLibraries
{
    // Usage:
    // Console.WriteLine(BlePayloadBuilder.BuildPayloadHex(0x01));
    /*
      [channel][epoch UTC seconds (4 bytes, big-endian)][HMAC-SHA256 first 12 bytes]
      17 bytes total → 34 hex characters. 
    
      [ 1 byte channel ]
      [ 4 bytes epoch-seconds (UTC, big-endian) ]
      [ 12 bytes HMAC-SHA256 truncated ]
      = 17 bytes total
      = 34 hex characters

      Example:
      > 0101C1FAC679937BDAC1BE6064E7FC447D
      > 01 | 01 C1 FA C6 | 79 93 7B DA C1 BE 60 64 E7 FC 44 7D
     */
    public static class BlePayloadBuilder
    {
        private static readonly byte[] SecretKey = Encoding.ASCII.GetBytes("key-fsa-relay");

        // Builds the 17-byte binary payload:
        // [1 byte channel][4 bytes UTC epoch seconds big-endian][12 bytes HMAC]
        public static byte[] BuildPayload(byte channel)
        {
            // UTC seconds since 1970
            uint epoch = (uint)DateTimeOffset.UtcNow.ToUnixTimeSeconds();

            // msg = channel + epoch seconds (big-endian)
            byte[] msg = new byte[5];
            msg[0] = channel;
            msg[1] = (byte)(epoch >> 24);
            msg[2] = (byte)(epoch >> 16);
            msg[3] = (byte)(epoch >> 8);
            msg[4] = (byte)(epoch);

            byte[] fullMac;
            using (var h = new HMACSHA256(SecretKey))
            {
                fullMac = h.ComputeHash(msg);
            }

            // payload = msg + first 12 bytes of HMAC
            byte[] payload = new byte[17];
            Buffer.BlockCopy(msg, 0, payload, 0, 5);
            Buffer.BlockCopy(fullMac, 0, payload, 5, 12);

            return payload;
        }

        // Builds the 34-hex-character string for BLE tools (nRF Connect, etc.)
        public static string BuildPayloadHex(byte channel)
        {
            ulong epochSec = (ulong)DateTimeOffset.UtcNow.ToUnixTimeSeconds();

            Console.WriteLine($"UTC now  : {DateTimeOffset.UtcNow:O}");
            Console.WriteLine($"epochSec : {epochSec}");

            byte[] payload = BuildPayload(channel);
            string hex = BitConverter.ToString(payload).Replace("-", "");

            Console.WriteLine($"HEX(34)  : {hex}");
            Console.WriteLine($"LEN      : {hex.Length}");

            return hex;
        }
    }
}