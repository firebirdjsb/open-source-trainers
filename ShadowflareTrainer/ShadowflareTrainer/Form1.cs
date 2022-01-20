using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using Memory;
using System.Threading;

namespace ShadowflareTrainer
{
    public partial class Form1 : Form
    {
        private bool _dragging = false;
        private Point _start_point = new Point(0, 0);
        public Form1()
        {
            InitializeComponent();
        }

        public Mem m = new Mem();
        //"ShadowFlare.exe"+0008CE0C + 1A4 + 0 + 4
        string healthOffset = "base+0x43ED4C,29,BE,AC,01,00,00";
        string manaOffset1 = "base+0x428CB,89,86,AC,01,00,00";
        string manaOffset2 = "base+0x4F5C5,89,86,AC,01,00,00";
        string mannaOffset3 = "base+0x442F4,89,8E,AC,01,00,00";
        string expOffset = "base+0x1357B,89,86,E8,00,00,00";
        string Y = "0x085844FC";
        string X = "0x085844F8";
        private void Form1_Load(object sender, EventArgs e)
        {
            if (!backgroundWorker1.IsBusy)
                backgroundWorker1.RunWorkerAsync();

        }


        private void backgroundWorker1_DoWork(object sender, DoWorkEventArgs e)
        {
            while (true)
            {

                int pID = m.GetProcIdFromName("ShadowFlare");//get proc id of game
                bool openProc = false;//is proc open
                if (pID > 0)
                {
                    openProc = m.OpenProcess(pID);//try opening proc if proc id greater then 1
                    procIDlabel.Invoke((MethodInvoker)delegate
                    {
                        procIDlabel.Text = pID.ToString();
                    });


                }

                if (openProc)//if proc id open, RUN CODE
                {
                    OpenLabel.Invoke((MethodInvoker)delegate
                    {
                        OpenLabel.Text = "FOUND";
                        OpenLabel.ForeColor = Color.Green;
                    });

                }
            }

        }
        //form drag
        private void panel1_MouseDown(object sender, MouseEventArgs e)
        {
            _dragging = true;
            _start_point = new Point(e.X, e.Y);
        }
        //form drag
        private void panel1_MouseMove(object sender, MouseEventArgs e)
        {
            if (_dragging)
            {
                Point p = PointToScreen(e.Location);
                Location = new Point(p.X - this._start_point.X, p.Y - this._start_point.Y);
            }
        }
        //form drag
        private void panel1_MouseUp(object sender, MouseEventArgs e)
        {
            _dragging = false;
        }

        private void button2_Click(object sender, EventArgs e)
        {
            Application.Exit();
        }

        private void button1_Click(object sender, EventArgs e)
        {
            this.WindowState = FormWindowState.Minimized;
        }

        private void checkBox1_CheckedChanged(object sender, EventArgs e)
        {
            if (godMode.Checked)
            {
                m.WriteMemory("base+0x43EDC", "bytes", "90 90 90 90 90 90"); //NOP
            }
            else
            {
                m.WriteMemory("base+0x43EDC", "bytes", "29 BE A4 01 00 00");
            }
        }

        private void checkBox2_CheckedChanged(object sender, EventArgs e)
        {
            if (unlimitedMana.Checked)
            {
                //adds mana if you dont have any
                m.WriteMemory("0x086B43C4", "int", "800");

                m.WriteMemory("base+0x428CB", "bytes", "90 90 90 90 90 90");
                m.WriteMemory("base+0x4F5C5", "bytes", "90 90 90 90 90 90");
                m.WriteMemory("base+0x442F4", "bytes", "90 90 90 90 90 90");
                m.WriteMemory("base+0x43ECC", "bytes", "90 90 90 90 90 90");
            }
            else
            {
                m.WriteMemory("base+0x428CB", "bytes", "89 86 AC 01 00 00");
                m.WriteMemory("base+0x4F5C5", "bytes", "89 86 AC 01 00 00");
                m.WriteMemory("base+0x442F4", "bytes", "89 8E AC 01 00 00");
                m.WriteMemory("base+0x43ECC", "bytes", "89 86 AC 01 00 00");
            }
        }

        private void unlimitedBombs_CheckedChanged(object sender, EventArgs e)
        {
            if (unlimitedBombs.Checked)
            {
                m.WriteMemory("0x086B4540", "int", "99");
                m.WriteMemory("base+0x430A7", "bytes", "90 90 90 90 90 90");


            }
            else
            {
                m.WriteMemory("0x086B4540", "int", "10");
                m.WriteMemory("base+0x430A7", "bytes", "89 8E 28 03 00 00");
            }

        }

        private void EXP_CheckedChanged(object sender, EventArgs e)

        {
            if (EXP.Checked)
            {
                do
                {
                    
                    m.WriteMemory("0x08824300", "bytes", "FF E3 0B 54");
                    Application.DoEvents();



                } while (EXP.Checked);
            }
            else if (!EXP.Checked)
            {
                m.WriteMemory("0x08824300", "bytes", "00 00 00 00");
            }

        }
        private void dogXp_CheckedChanged(object sender, EventArgs e)
        {
            if (dogXp.Checked)
            {
                do
                {

                    m.WriteMemory("0x08584370", "bytes", "FF E3 0B 54");
                    Application.DoEvents();
                } while (dogXp.Checked);

            }
            else if (!dogXp.Checked)
            {
                m.WriteMemory("0x08584370", "bytes", "00 00 00 00");

            }

        }
        private void attackSpeed_CheckedChanged(object sender, EventArgs e)
        {
            if (attackSpeed.Checked)
            {
                do
                {
                    m.WriteMemory("0x085843B0", "int", "999");
                    Application.DoEvents();
                } while (attackSpeed.Checked);
                
            }
        }

        private void mageStats_CheckedChanged(object sender, EventArgs e)
        {
            /*
            if (mageStats.Checked)
            {
                do
                {
                   // m.WriteMemory("base+0x4F776", "int", "20");
                    Application.DoEvents();
                } while (mageStats.Checked);
            }
            else if (!mageStats.Checked)
            {
               // m.WriteMemory("base+0x4F776", "int", "1");
            }
         */      
        }
        




        private void button3_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844F8", "int", "90581");
            m.WriteMemory("0x085844FC", "int", "5288");
        }

        private void button4_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844F8", "int", "75343");
            m.WriteMemory("0x085844FC", "int", "5856");
        }

        private void button5_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844F8", "int", "68651");
            m.WriteMemory("0x085844FC", "int", "15038");
        }

        private void button6_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844F8", "int", "68170");
            m.WriteMemory("0x085844FC", "int", "3369");
        }

        private void button7_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "1A E7 FF FF");
            m.WriteMemory("0x085844F8", "int", "70333");
        }

        private void button8_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844F8", "int", "80483");
            m.WriteMemory("0x085844FC", "int", "6396");
        }

        private void button9_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "31 C1 FF FF");
            m.WriteMemory("0x085844F8", "int", "40968");
        }

        private void button10_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "06 F1 FF FF");
            m.WriteMemory("0x085844F8", "int", "44173");
        }

        private void button11_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "9C E2 FF FF");
            m.WriteMemory("0x085844F8", "int", "70083");
        }

        private void button12_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "8D BF FF FF");
            m.WriteMemory("0x085844F8", "int", "40068");
        }

        private void button13_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "1C D3 FF FF");
            m.WriteMemory("0x085844F8", "int", "25061");
        }

        private void button14_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "01 00 00 00");
            m.WriteMemory("0x085844F8", "int", "21528");
        }

        private void button15_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "F4 E7 FF FF");
            m.WriteMemory("0x085844F8", "int", "35105");
        }

        private void button16_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "EA EE FF FF");
            m.WriteMemory("0x085844F8", "int", "43033");
        }

        private void button17_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "1F 0D 00 00");
            m.WriteMemory("0x085844F8", "int", "67200");
        }

        private void button18_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "BE 3A 00 00");
            m.WriteMemory("0x085844F8", "int", "67531");
        }

        private void button19_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "5C 01 00 00");
            m.WriteMemory("0x085844F8", "int", "113261");
        }

        private void button20_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "E6 13 00 00");
            m.WriteMemory("0x085844F8", "int", "90429");
        }

        private void button21_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "4A AB FF FF");
            m.WriteMemory("0x085844F8", "int", "75615");
        }

        private void button22_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "58 F7 FF FF");
            m.WriteMemory("0x085844F8", "int", "84385");
        }

        private void button23_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "68 47 00 00");
            m.WriteMemory("0x085844F8", "int", "96900");
        }

        private void button24_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "2C 4B 00 00");
            m.WriteMemory("0x085844F8", "int", "97305");
        }

        private void button25_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "3E 4F 00 00");
            m.WriteMemory("0x085844F8", "int", "104593");
        }

        private void button26_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "1D 50 00 00");
            m.WriteMemory("0x085844F8", "int", "108789");
        }

        private void button27_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "4F 65 00 00");
            m.WriteMemory("0x085844F8", "int", "84595");
        }

        private void button28_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "6C 64 00 00");
            m.WriteMemory("0x085844F8", "int", "83421");
        }

        private void button29_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "1F 4A 00 00");
            m.WriteMemory("0x085844F8", "int", "74651");
        }

        private void button30_Click(object sender, EventArgs e)
        {
            m.WriteMemory("0x085844FC", "bytes", "D4 42 00 00");
            m.WriteMemory("0x085844F8", "int", "76021");
        }

    }
}

