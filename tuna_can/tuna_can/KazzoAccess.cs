using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using LibUsbDotNet;
using LibUsbDotNet.LibUsb;
using LibUsbDotNet.Main;

namespace tuna_can
{
    class KazzoAccess
    {
        private const int VenderId = 0x16C0;
        private const int ProductId = 0x05DC;
        private const string ProductName = "kazzo";
        private static UsbSetupPacket setupPacketIn = new UsbSetupPacket((byte)UsbRequestType.TypeVendor | (byte)UsbCtrlFlags.Direction_In | (byte)UsbCtrlFlags.Recipient_Device, 0, 0, 0, 0);
        private static UsbSetupPacket setupPacketOut = new UsbSetupPacket((byte)UsbRequestType.TypeVendor | (byte)UsbCtrlFlags.Direction_Out | (byte)UsbCtrlFlags.Recipient_Device, 0, 0, 0, 0);

        private enum KAZZO_REQUEST : byte
        {
            ECHO = 0,

            PHI2_INIT,
            CPU_READ_6502,
            CPU_READ,
            CPU_WRITE_6502,
            CPU_WRITE_FLASH,
            PPU_READ,
            PPU_WRITE,
            FLASH_STATUS,
            FLASH_CONFIG_SET,
            FLASH_PROGRAM,
            FLASH_ERASE,
            FLASH_DEVICE,
            VRAM_CONNECTION,

            // future expanstion (not support)
            DISK_STATUS_GET,
            DISK_READ,
            DISK_WRITE,

            // bootloader control (not support)
            FIRMWARE_VERSION = 0x80,
            FIRMWARE_PROGRAM,
            FIRMWARE_DOWNLOAD,

            // internal use
            NOP = 0xEE
        };

        static public bool EchoTest()
        {
            UsbDevice dev = OpenUSBDevice();
            if (dev != null)
            {
                setupPacketIn.Value = 0x1234;
                setupPacketIn.Index = 0x4567;

                IntPtr buffer = Marshal.AllocCoTaskMem(4);
                int length = 0;

                if (dev.ControlTransfer(ref setupPacketIn, buffer, 4, out length))
                {
                    if (length == 4)
                    {
                        byte[] retval = new byte[4];
                        Marshal.Copy(buffer, retval, 0, 4);
                        System.Console.WriteLine("RET:" + BitConverter.ToString(retval));
                    } else
                    {
                        System.Console.WriteLine("return length error." + length.ToString());
                    }
                }
                else
                {
                    System.Console.WriteLine("ControlTransfer failed?");
                }
                Marshal.FreeCoTaskMem(buffer);

                dev.Close();
            }

            return false;
        }

        static private bool WriteData(byte request, ushort addr, byte[] data)
        {
            using (UsbDevice dev = OpenUSBDevice())
            {
                if (dev == null)
                    return false;

                IntPtr buf = IntPtr.Zero;
                try
                {
                    // make send data
                    byte[] senddata = new byte[data.Length];
                    for (int i = 0; i < data.Length; i++)
                    {
                        senddata[i] = (byte)(data[i] ^ 0xA5);
                    }

                    buf = Marshal.AllocCoTaskMem(senddata.Length);
                    Marshal.Copy(senddata, 0, buf, senddata.Length);

                    int transferredlength = 0;

                    setupPacketOut.Request = (byte)request;
                    setupPacketOut.Value = (short)addr;
                    setupPacketOut.Index = 0;
                    setupPacketOut.Length = (short)senddata.Length;

                    if (dev.ControlTransfer(ref setupPacketOut, buf, setupPacketOut.Length, out transferredlength))
                    {
                        if (transferredlength != senddata.Length)
                            return false;

                        return true;
                    }
                }
                finally
                {
                    if (buf != IntPtr.Zero)
                        Marshal.FreeCoTaskMem(buf);
                }
            }

            return false;
        }

        static private bool ReadData(byte request, ushort addr, ref byte[] data)
        {
            using (UsbDevice dev = OpenUSBDevice())
            {
                if (dev == null)
                    return false;

                IntPtr buf = IntPtr.Zero;
                try
                {
                    buf = Marshal.AllocCoTaskMem(data.Length);

                    int transferredlength = 0;

                    setupPacketIn.Request = request;
                    setupPacketIn.Value = (short)addr;
                    setupPacketIn.Index = 0;
                    setupPacketIn.Length = (short)data.Length;

                    if (dev.ControlTransfer(ref setupPacketIn, buf, setupPacketIn.Length, out transferredlength))
                    {
                        if (transferredlength != data.Length)
                            return false;

                        Marshal.Copy(buf, data, 0, data.Length);
                        return true;
                    }
                }
                finally
                {
                    if (buf != IntPtr.Zero)
                        Marshal.FreeCoTaskMem(buf);
                }
            }

            return false;
        }

        static public bool PrgWriteData(ushort addr, byte[] data)
        {
            return WriteData((byte)KAZZO_REQUEST.CPU_WRITE_6502, addr, data);
        }

        static public bool PrgWriteDataLowFix(ushort addr, byte[] data)
        {
            return WriteData((byte)KAZZO_REQUEST.CPU_WRITE_FLASH, addr, data);
        }

        static public bool PrgReadData(ushort addr, ref byte[] data)
        {
            return ReadData((byte)KAZZO_REQUEST.CPU_READ_6502, addr, ref data);
        }

        static public bool ChrWriteData(ushort addr, byte[] data)
        {
            return WriteData((byte)KAZZO_REQUEST.PPU_WRITE, addr, data);
        }

        static public bool ChrReadData(ushort addr, ref byte[] data)
        {
            return ReadData((byte)KAZZO_REQUEST.PPU_READ, addr, ref data);
        }

        static public bool PpuRamAnalyze(ref bool ram)
        {
            byte[] buf0 = new byte[1];
            byte[] buf1 = new byte[1];
            ram = false;

            buf0[0] = 0x55;
            if (!ChrWriteData(0x0000, buf0))
                return false;

            if (!ChrReadData(0x0000, ref buf1))
                return false;

            System.Console.WriteLine("buf0:" + BitConverter.ToString(buf0));
            System.Console.WriteLine("buf1:" + BitConverter.ToString(buf1));

            if (buf0[0] != buf1[0])
                return true;

            buf0[0] = 0xAA;
            if (!ChrWriteData(0x0000, buf0))
                return false;

            if (!ChrReadData(0x0000, ref buf1))
                return false;

            if (buf0[0] != buf1[0])
                return true;

            ram = true;
            return true;
        }

        static public bool PpuMirrorAnalyze(ref byte mirror)
        {
            using (UsbDevice dev = OpenUSBDevice())
            {
                if (dev == null)
                    return false;

                IntPtr buf = IntPtr.Zero;
                try
                {
                    buf = Marshal.AllocCoTaskMem(1);

                    int transferredlength = 0;

                    setupPacketIn.Request = (byte)KAZZO_REQUEST.VRAM_CONNECTION;
                    setupPacketIn.Value = 0;
                    setupPacketIn.Index = 0;
                    setupPacketIn.Length = 1;

                    if (dev.ControlTransfer(ref setupPacketIn, buf, setupPacketIn.Length, out transferredlength))
                    {
                        mirror = Marshal.ReadByte(buf);
                        return true;
                    }
                }
                finally
                {
                    if (buf != IntPtr.Zero)
                        Marshal.FreeCoTaskMem(buf);
                }
            }

            return false;
        }

        static public bool CartridgeTest(out bool available)
        {
            byte[] buf0 = new byte[16];
            byte[] buf1 = new byte[16];
            byte[] buf2 = new byte[16];

            available = false;

            if (!PrgReadData(0xF000, ref buf0))
                return false;
            if (!PrgReadData(0xF000, ref buf1))
                return false;
            if (!PrgReadData(0xF000, ref buf2))
                return false;

            if (!buf0.SequenceEqual(buf1))
                return true;
            if (!buf0.SequenceEqual(buf2))
                return true;

            available = true;

            return true;
        }



        // USB device open
        static public UsbDevice OpenUSBDevice()
        {
            // find kazzo
            UsbDevice usbdevice = null;
            UsbRegDeviceList allDevices = UsbDevice.AllDevices;
            foreach (UsbRegistry usbRegistry in allDevices)
            {
                if (usbRegistry.Open(out usbdevice))
                {
                    if((usbdevice.Info.Descriptor.VendorID == VenderId) && (usbdevice.Info.Descriptor.ProductID == ProductId) && (usbdevice.Info.ProductString == ProductName))
                    {
                        // found USB device
                        return usbdevice;
                    }
                    else
                    {
                        usbdevice.Close();
                    }
                }
            }

            return usbdevice;
        }
    }
}
