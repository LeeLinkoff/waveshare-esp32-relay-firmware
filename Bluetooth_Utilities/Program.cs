using System;
using SecurityRelatedLibraries;

namespace Bluetooth_Utilities
{
    internal class Program
    {
        static void Main()
        {
            Console.WriteLine("Payload: " + BlePayloadBuilder.BuildPayloadHex((byte) 1));
            Console.ReadKey();
        }
    }
}
