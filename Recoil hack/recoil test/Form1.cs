using Gma.System.MouseKeyHook;
using InputClass;
using System;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Timers;
using System.Windows.Forms;

namespace recoil_test
{

    public partial class Form1 : Form
    {
        private bool _dragging = false;
        private Point _start_point = new Point(0, 0);


        [DllImport("user32.dll")]
        private static extern bool SetWindowPos(int hWnd, int hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);


        private static void ShowInactiveTopmost(Form frm)
        {
            Form1.ShowWindow(frm.Handle, 4);
            Form1.SetWindowPos(frm.Handle.ToInt32(), -1, frm.Left, frm.Top, frm.Width, frm.Height, 16U);
        }


        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);


        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();


        public Form1()
        {
            this.InitializeComponent();
            this.SubscribeGlobal();
            this.KeyPreview = true;
        }


        private void SubscribeGlobal()
        {
            this.Unsubscribe();
            this.Subscribe(Hook.GlobalEvents());
        }


        private void Subscribe(IKeyboardMouseEvents events)
        {
            this.m_Events = events;
            this.m_Events.KeyDown += this.OnKeyDown;
            this.m_Events.MouseDown += this.OnMouseDown;
            this.m_Events.MouseUp += this.OnMouseUp;
        }


        public void Log(string oks)
        {
            Console.WriteLine(oks);
        }


        public void recoilTimerEvent(object source, ElapsedEventArgs e)
        {
            int num = (int)this.stopWatch.ElapsedMilliseconds;
            this.currentrecoil = this.recoil + this.loops / this.increasespeed;
            this.loops++;
        }


        public void speedTimerEvent(object source, ElapsedEventArgs e)
        {
            Class1.MouseMoves(0, this.currentrecoil);
            Console.WriteLine(this.currentrecoil / 2);
        }


        private void OnKeyDown(object sender, KeyEventArgs e)
        {
            bool flag = e.KeyCode == Keys.Insert;
            if (flag)
            {
                bool flag2 = !this.activated;
                if (flag2)
                {
                    this.secondForm.setON();
                    this.activated = true;
                    Form1.GetForegroundWindow();
                }
                else
                {
                    bool flag3 = this.activated;
                    if (flag3)
                    {
                        this.activated = false;
                        this.secondForm.setOFF();
                    }
                }
                Console.WriteLine(string.Format("Recoil ON\n", e.KeyCode));
            }
            bool flag4 = e.KeyCode == Keys.Down;
            if (flag4)
            {
                this.recoil--;
                this.secondForm.updateRecoil(this.recoil);

                if (recoil <= 1)
                {
                    this.recoil = 1;

                    this.secondForm.updateRecoil(this.recoil);


                }
            }
            bool flag5 = e.KeyCode == Keys.Up;
            if (flag5)
            {
                this.recoil++;
                this.secondForm.updateRecoil(this.recoil);
            }
            bool flag6 = e.KeyCode == Keys.Right;
            if (flag6)
            {
                this.bulletspeed++;
                this.secondForm.updateSpeed(this.bulletspeed);
                this.speedTimer.Interval = (double)this.bulletspeed;
            }
            bool flag7 = e.KeyCode == Keys.Left;
            if (flag7)
            {
                this.bulletspeed--;
                this.secondForm.updateSpeed(this.bulletspeed);

                if (this.bulletspeed <= 1)
                {
                    this.bulletspeed = 1;
                    this.speedTimer.Interval = (double)this.bulletspeed;
                    this.secondForm.updateSpeed(this.bulletspeed);
                }

            }

            bool flag8 = e.KeyCode == Keys.NumPad9;
            if (flag8)
            {
                this.increasespeed++;
                this.secondForm.updateIncrease(this.increasespeed);
            }
            bool flag9 = e.KeyCode == Keys.NumPad3;
            if (flag9)
            {
                this.increasespeed--;
                this.secondForm.updateIncrease(this.increasespeed);
                if (this.increasespeed <= 1)
                {
                    this.increasespeed = 1;
                    this.secondForm.updateIncrease(this.increasespeed);
                }
            }
        }


        public void OnMouseDown(object sender, MouseEventArgs e)
        {
            bool flag = !this.activated;
            if (!flag)
            {
                bool flag2 = e.Button == MouseButtons.Right;
                if (!flag2)
                {
                    this.loops = 0;
                    this.currentrecoil = this.recoil;
                    this.stopWatch.Reset();
                    this.stopWatch.Start();
                    this.recoilTimer.Enabled = true;
                    this.speedTimer.Enabled = true;
                }
            }
        }

        public void OnMouseUp(object sender, MouseEventArgs e)
        {
            bool flag = !this.activated;
            if (!flag)
            {
                bool flag2 = e.Button == MouseButtons.Right;
                if (!flag2)
                {
                    this.loops = 0;
                    this.recoilTimer.Enabled = false;
                    this.speedTimer.Enabled = false;
                    this.stop = true;
                    this.stopWatch.Stop();
                }
            }
        }


        private void Unsubscribe()
        {
            bool flag = this.m_Events == null;
            if (flag)
            {
            }
        }


        private void Form1_Load(object sender, EventArgs e)
        {
            this.secondForm.Show();
            Form1.ShowInactiveTopmost(this.secondForm);
            this.recoilTimer = new System.Timers.Timer();
            this.recoilTimer.Interval = 90.0;
            this.recoilTimer.Enabled = false;
            this.recoilTimer.Elapsed += this.recoilTimerEvent;
            this.recoilTimer.AutoReset = true;
            this.speedTimer = new System.Timers.Timer();
            this.speedTimer.Interval = 90.0;
            this.speedTimer.Enabled = false;
            this.speedTimer.Elapsed += this.speedTimerEvent;
            this.speedTimer.AutoReset = true;
            this.secondForm.updateIncrease(this.increasespeed);
            this.secondForm.updateRecoil(this.recoil);
            this.secondForm.updateSpeed(this.bulletspeed);
        }


        private System.Timers.Timer recoilTimer;


        private System.Timers.Timer speedTimer;


        private int loops = 0;


        private int speed = 100;


        private int increasespeed =1;


        private Stopwatch stopWatch = new Stopwatch();


        private bool stop = false;


        private int recoil = 1;


        private int bulletspeed = 1;


        private int currentrecoil = 1;


        private bool activated = false;


        private Form2 secondForm = new Form2();


        public const int SW_SHOWNOACTIVATE = 4;


        public const int HWND_TOPMOST = -1;


        public const uint SWP_NOACTIVATE = 16U;


        private IKeyboardMouseEvents m_Events;

    

        private void button1_Click(object sender, EventArgs e)
        {
            if (System.Windows.Forms.Application.MessageLoop)
            {
                // WinForms app
                Application.Exit();
            }
            else
            {
                // Console app
                Environment.Exit(1);
            }
        }

        private void button2_Click(object sender, EventArgs e)
        {
            this.WindowState = FormWindowState.Minimized;
        }

        private void panel1_MouseDown(object sender, MouseEventArgs e)
        {
            _dragging = true;
            _start_point = new Point(e.X, e.Y);
        }

        private void panel1_MouseUp(object sender, MouseEventArgs e)
        {
            _dragging = false;
        }

        private void panel1_MouseMove(object sender, MouseEventArgs e)
        {
            if (_dragging)
            {
                Point p = PointToScreen(e.Location);
                Location = new Point(p.X - this._start_point.X, p.Y - this._start_point.Y);
            }
        }

        private void checkBox1_CheckedChanged_1(object sender, EventArgs e)
        {

        }


        private void Form1_KeyDown(object sender, KeyEventArgs e)
        {

        }
    }
}
