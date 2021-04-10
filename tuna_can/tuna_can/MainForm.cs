using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Collections.ObjectModel;
using LibUsbDotNet;
using LibUsbDotNet.LibUsb;
using LibUsbDotNet.Info;
using LibUsbDotNet.Main;

namespace tuna_can
{
    public partial class MainForm : Form
    {
		// WndProcフック用
		private WndCmdHook wphook;

		// コマンドラインの履歴
		private List<string> cmdhistory = new List<string>();
		private int cmdhistoryindex;

		public MainForm()
		{
			InitializeComponent();

			wphook = new WndCmdHook(tbCommandline);

			// どちらでも可
			wphook.COMMANDPREV += () => { onCommandPrev(); };
			wphook.COMMANDNEXT += () => { onCommandNext(); };
			wphook.COMMANDEXEC += () => { onCommandExec(); };
			//wphook.COMMANDPREV += new Action(onCommandPrev);
			//wphook.COMMANDNEXT += new Action(onCommandNext);
			//wphook.COMMANDEXEC += new Action(onCommandExec);
		}

		public void Logout(string str)
		{
			tbLog.AppendText(str.Replace("\n", "\r\n"));
		}

		private void onLogClear(object sender, EventArgs e)
		{
			tbLog.Clear();
			KazzoAccess.EchoTest();
		}

		private void onCommandPrev()
		{
//			Logout("onCommandPrev\n");

			if (cmdhistory.Count > 0)
			{
				--cmdhistoryindex;
				if (cmdhistoryindex < 0)
				{
					cmdhistoryindex = cmdhistory.Count - 1;
				}

				tbCommandline.Text = cmdhistory[cmdhistoryindex];
				tbCommandline.Select(tbCommandline.Text.Length, 0);
			}
		}

		private void onCommandNext()
		{
//			Logout("onCommandNext\n");

			if (cmdhistory.Count > 0)
			{
				++cmdhistoryindex;
				if (cmdhistoryindex > (cmdhistory.Count - 1))
				{
					cmdhistoryindex = 0;
				}
				tbCommandline.Text = cmdhistory[cmdhistoryindex];
				tbCommandline.Select(tbCommandline.Text.Length, 0);
			}
		}

		private void onCommandExec()
		{
			string str = tbCommandline.Text;

			cmdhistory.Add(str);
			if (cmdhistory.Count > 64)
			{
				cmdhistory.RemoveAt(0);
			}
			cmdhistoryindex = cmdhistory.Count;

			tbCommandline.Clear();

			// command exec
			Logout(str.ToUpper() + "\n");
			CommandlineProcess(str);
		}

		private void onShown(object sender, EventArgs e)
		{
			DispHelp();
		}

		// command line process
		private void DispHelp()
		{
			Logout("Command help:\n");
			Logout("H or ?              This message\n");
			Logout("D  <addr>           PRG dump <addr>\n");
			Logout("CD <addr>           CHR dump <addr>\n");
			Logout("W  addr data <...>  PRG write addr\n");
			Logout("W2 addr data <...>  PRG write addr (PHI2 'L' fixed)\n");
			Logout("R  addr             PRG read addr\n");
			Logout("CW addr data <...>  CHR write addr\n");
			Logout("CR addr             CHR read addr\n");
			Logout("PA                  PPU ram analyze\n");
			Logout("MA                  Mirror analyze\n");
			Logout("\n");
		}

		private void DumpMemory(bool type, ushort addr)
        {
			byte[] buf = new byte[128];

			if (type)
			{
				if (!KazzoAccess.PrgReadData(addr, ref buf))
				{
					Logout("device not found.\n");
					return;
				}
				Logout("PRG DUMP\n");
			}
			else
			{
				addr &= 0x3FFF;
				if (!KazzoAccess.ChrReadData(addr, ref buf))
				{
					Logout("device not found.\n");
					return;
				}
				Logout("CHR DUMP\n");
			}

			Logout("ADDR : +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F ASCII\n" + 
				   "-----+------------------------------------------------+----------------\n");
			for (int i = 0; i < 8; i++)
            {
				string str = (addr + i * 16).ToString("X4") + " : ";
				for (int j = 0; j < 16; j++)
                {
					str = str + buf[i * 16 + j].ToString("X2") + " ";
                }
				for (int j = 0; j < 16; j++)
                {
					char c = (char)buf[i * 16 + j];
					if (c >= 0x20 && c <= 0x7F)
					{
						str = str + c.ToString();
					}
					else
					{
						str = str + ".";
					}
				}
				str = str + "\n";
				Logout(str);
			}
		}

		private bool CartridgeTest()
        {
			bool available;
			if (!KazzoAccess.CartridgeTest(out available))
			{
				Logout("device not found.\n");
				return false;
			}
			else
			{
				if (!available)
				{
					Logout("may be cartridge power off or not exist.\n");
					return false;
				}
			}

			return true;
		}

		private ushort prgaddr = 0;
		private ushort chraddr = 0;

		private void CommandlineProcess(string cmd)
        {
			if (cmd == null)
				return;


			string[] cmds = cmd.Split(' ');

			switch(cmds[0].ToUpper())
            {
				case "H":
				case "?":
					DispHelp();
					break;
				case "D":
                    {
						if (!CartridgeTest())
							break;

						if (cmds.Length < 2)
						{
							DumpMemory(true, prgaddr);
							prgaddr += 128;
						}
						else
						{
							prgaddr = (ushort)Convert.ToInt32(cmds[1], 16);
							DumpMemory(true, prgaddr);
							prgaddr += 128;
						}
                    }
					break;
				case "CD":
					{
						if (!CartridgeTest())
							break;

						if (cmds.Length < 2)
						{
							DumpMemory(false, chraddr);
							chraddr += 128;
							chraddr &= 0x3FFF;
						}
						else
						{
							chraddr = (ushort)Convert.ToInt32(cmds[1], 16);
							chraddr &= 0x3FFF;
							DumpMemory(false, chraddr);
							chraddr += 128;
						}
					}
					break;
				case "W":
					{
						if (!CartridgeTest())
							break;

						if (cmds.Length > 2)
						{
							prgaddr = (ushort)Convert.ToInt32(cmds[1], 16);

							byte[] wdata = new byte[cmds.Length - 2];
							for (int i = 0; i < cmds.Length - 2; i++)
							{
								wdata[i] = (byte)Convert.ToInt32(cmds[i + 2], 16);
							}

							if (!KazzoAccess.PrgWriteData(prgaddr, wdata))
							{
								Logout("device not found.\n");
								return;
							}

							Logout("PRG WRITE: " + prgaddr.ToString("X4") + " ");
							for (int i = 0; i < cmds.Length - 2; i++)
							{
								Logout(wdata[i].ToString("X2") + " ");
							}
							Logout("\n");

							prgaddr += (ushort)(cmds.Length - 2);
						}
					}
					break;
				case "W2":
					{
						if (!CartridgeTest())
							break;

						if (cmds.Length > 2)
						{
							prgaddr = (ushort)Convert.ToInt32(cmds[1], 16);

							byte[] wdata = new byte[cmds.Length - 2];
							for (int i = 0; i < cmds.Length - 2; i++)
							{
								wdata[i] = (byte)Convert.ToInt32(cmds[i + 2], 16);
							}

							if (!KazzoAccess.PrgWriteDataLowFix(prgaddr, wdata))
							{
								Logout("device not found.\n");
								return;
							}

							Logout("PRG WRITE: " + prgaddr.ToString("X4") + " ");
							for (int i = 0; i < cmds.Length - 2; i++)
							{
								Logout(wdata[i].ToString("X2") + " ");
							}
							Logout("\n");

							prgaddr += (ushort)(cmds.Length - 2);
						}
					}
					break;
				case "R":
					{
						if (!CartridgeTest())
							break;

						if (cmds.Length > 1)
						{
							prgaddr = (ushort)Convert.ToInt32(cmds[1], 16);
						}

						byte[] buf = new byte[1];

						if (!KazzoAccess.PrgReadData(prgaddr, ref buf))
						{
							Logout("device not found.\n");
							return;
						}

						Logout("PRG READ: " + prgaddr.ToString("X4") + " ");
						Logout(buf[0].ToString("X2"));
						Logout("\n");

						prgaddr += 1;
					}
					break;
				case "CW":
					{
						if (!CartridgeTest())
							break;

						if (cmds.Length > 2)
						{
							chraddr = (ushort)Convert.ToInt32(cmds[1], 16);
							chraddr &= 0x3FFF;

							byte[] wdata = new byte[cmds.Length - 2];
							for (int i = 0; i < cmds.Length - 2; i++)
							{
								wdata[i] = (byte)Convert.ToInt32(cmds[i + 2], 16);
							}

							if (!KazzoAccess.ChrWriteData(chraddr, wdata))
							{
								Logout("device not found.\n");
								return;
							}

							Logout("CHR WRITE: " + chraddr.ToString("X4") + " ");
							for (int i = 0; i < cmds.Length - 2; i++)
							{
								Logout(wdata[i].ToString("X2") + " ");
							}
							Logout("\n");

							chraddr += (ushort)(cmds.Length - 2);
						}
					}
					break;
				case "CR":
					{
						if (!CartridgeTest())
							break;

						if (cmds.Length > 1)
						{
							chraddr = (ushort)Convert.ToInt32(cmds[1], 16);
							chraddr &= 0x3FFF;
						}

						byte[] buf = new byte[1];

						if (!KazzoAccess.ChrReadData(chraddr, ref buf))
						{
							Logout("device not found.\n");
							return;
						}

						Logout("CHR READ: " + chraddr.ToString("X4") + " ");
						Logout(buf[0].ToString("X2"));
						Logout("\n");

						chraddr += 1;
						chraddr &= 0x3FFF;
					}
					break;
				case "PA":
                    {
						if (!CartridgeTest())
							break;

						bool ram = false;
						if (!KazzoAccess.PpuRamAnalyze(ref ram))
                        {
							Logout("device not found.\n");
							return;
						}

						if (ram)
						{
							Logout("PPU CHR RAM: YES\n");
						}
						else
						{
							Logout("PPU CHR RAM: NO\n");
						}
					}
					break;
				case "MA":
                    {
						if (!CartridgeTest())
							break;

						byte mirror = 0;
						if (!KazzoAccess.PpuMirrorAnalyze(ref mirror))
                        {
							Logout("device not found.\n");
							return;
						}

						Logout("Mirror type: ");
						switch (mirror)
                        {
							case 0x00:
								Logout("One screen $2000");
								break;
							case 0x0F:
								Logout("One screen $2400");
								break;
							case 0x0C:  // 1100
								Logout("H Mirror");
								break;
							case 0x0A:  // 1010
								Logout("V Mirror");
								break;
							default:
								Logout("Unknown.");
								break;
                        }
						string s = Convert.ToString(mirror, 2);
						s = s.PadLeft(4, '0');
						Logout(" (value = " + s + ")\n");
					}
					break;
				default:
					Logout("Unknown command.\n");
					break;
            }

        }

        private void onKeyPress(object sender, KeyPressEventArgs e)
        {
			if (e.KeyChar == (char)Keys.Enter)
            {
				e.Handled = true;
            }
        }
    }

    // WndProc処理用
    internal class WndCmdHook : NativeWindow
	{
		private Control parent;

		public Action COMMANDPREV { set; get; }
		public Action COMMANDNEXT { set; get; }
		public Action COMMANDEXEC { set; get; }

		public WndCmdHook(Control p)
		{
			parent = p;

			// SetWindowLongのサブクラス化のようなアレ
			parent.HandleCreated += (s, e) =>
			{
				AssignHandle((s as Control).Handle);
			};

			parent.HandleDestroyed += (s, e) =>
			{
				ReleaseHandle();
			};
		}

		protected override void WndProc(ref Message m)
		{
			const int WM_KEYDOWN = 0x100;
			const int WM_SYSKEYDOWN = 0x104;

			if (m.Msg == WM_KEYDOWN || m.Msg == WM_SYSKEYDOWN)
			{
				Keys ed = (Keys)m.WParam;

				if (ed == Keys.Up)
				{
					COMMANDPREV.Invoke();
				}
				else
				if (ed == Keys.Down)
				{
					COMMANDNEXT.Invoke();
				}
				else
				if (ed == Keys.Return)
				{
					COMMANDEXEC.Invoke();
				}
				else
				{
					base.WndProc(ref m);
				}
			}
			else
			{
				base.WndProc(ref m);
			}
		}
	}

}
