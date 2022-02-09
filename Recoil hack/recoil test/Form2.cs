using System;
using System.Drawing;
using System.Windows.Forms;

namespace recoil_test
{

    public partial class Form2 : Form
    {

        public Form2()
        {
            this.InitializeComponent();
        }


        protected override bool ShowWithoutActivation
        {
            get
            {
                return true;
            }
        }


        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams createParams = base.CreateParams;
                createParams.ExStyle |= 134217856;
                return createParams;
            }
        }


        private void Form2_Load(object sender, EventArgs e)
        {
            this.BackColor = Color.Red;
            Rectangle bounds = Screen.PrimaryScreen.Bounds;
            base.Location = new Point(bounds.Width - base.Size.Width, bounds.Height - base.Size.Height);
        }


        public void updateRecoil(int recoil)
        {
            this.label1.Text = recoil.ToString();
        }


        public void updateSpeed(int recoil)
        {
            this.label2.Text = recoil.ToString();
        }


        public void updateIncrease(int recoil)
        {
            this.label3.Text = recoil.ToString();
        }


        public void updateGun(string recoil)
        {
            this.label1.Text = recoil;
        }


        public void setON()
        {
            this.BackColor = Color.Green;
        }

        public void setOFF()
        {
            this.BackColor = Color.Red;
        }
    }
}
