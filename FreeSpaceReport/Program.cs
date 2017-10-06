using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;


namespace FreeSpaceReport
{
	class Program
	{
		static ulong GB = 1024 * 1024 * 1024;
		static List<Tuple<string, string>> m_credentials = new List<Tuple<string, string>>();
		static List<Tuple<string, int>> m_pathspec = new List<Tuple<string, int>>();

		static void Main()
		{
			ParseArgs();

			ulong available;
			ulong total;
			ulong unused;
			int ret;

			foreach(Tuple<string, int> pathspec in m_pathspec)
			{
				string spec = pathspec.Item1;
				if (spec[spec.Length - 1] != '\\')
				{
					spec += @"\";
				}

				if ((pathspec.Item2 < 0) || ((ret = WindowsRemoteAccess.connectToRemote(pathspec.Item1, m_credentials[pathspec.Item2].Item1, m_credentials[pathspec.Item2].Item2)) == 0))
				{
					if (External.GetDiskFreeSpaceEx(spec, out available, out total, out unused))
					{
						double size = (double)total / (double)GB;
						double percentUsed = (total - available) / (double)total;

						Console.WriteLine("{0} - {1:N2}GB, {2:P} full", pathspec.Item1, size, percentUsed);
					}
					else
					{
						Console.WriteLine("Unable to query {0}", pathspec.Item1);
					}

					if (pathspec.Item2 >= 0)
					{
						WindowsRemoteAccess.disconnectRemote(pathspec.Item1);
					}
				}
				else
				{
					Console.WriteLine("Unable to query {0} ({1})", pathspec.Item1, WindowsRemoteAccess.getErrorForNumber(ret));
				}
			}
		}

		static void ParseArgs()
		{
			string[] args = Environment.GetCommandLineArgs();

			string username = "";
			string password = "";

			for (int index = 1; index < args.Length; ++index)
			{
				if ((args[index].ToLower() == "--password") || (args[index].ToLower() == "-p") && (index < args.Length - 1))
				{
					password = args[++index];
					if (username != "")
					{
						m_credentials.Add(Tuple.Create(username, password));
						username = "";
						password = "";
					}
					continue;
				}

				if ((args[index].ToLower() == "--username") || (args[index].ToLower() == "-u") && (index < args.Length - 1))
				{
					username = args[++index];
					if (password != "")
					{
						m_credentials.Add(Tuple.Create(username, password));
						username = "";
						password = "";
					}
					continue;
				}

				m_pathspec.Add(Tuple.Create(args[index], m_credentials.Count - 1));
			}

			//foreach (Tuple<string, int> spec in m_pathspec)
			//{
			//	if (spec.Item2 < 0)
			//	{
			//		Console.WriteLine("[{0}] (no credentials)", spec.Item1);
			//	}
			//	else
			//	{
			//		Console.WriteLine("[{0}] ({1}:{2})", spec.Item1, m_credentials[spec.Item2].Item1, m_credentials[spec.Item2].Item2);
			//	}
			//}
		}
	}

	class External
	{
		[DllImport("Kernel32.dll", CharSet = CharSet.Auto)]
		internal static extern uint GetLastError();

		[DllImport("Kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
		[return: MarshalAs(UnmanagedType.Bool)]
		internal static extern bool GetDiskFreeSpaceEx(string lpDirectoryName,
			out ulong lpFreeBytesAvailable,
			out ulong lpTotalNumberOfBytes,
			out ulong lpTotalNumberOfFreeBytes);
	}
}
