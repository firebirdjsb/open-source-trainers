namespace recoil_test
{
	// Token: 0x02000004 RID: 4
	public partial class Form2 : global::System.Windows.Forms.Form
	{
		// Token: 0x06000023 RID: 35 RVA: 0x00002878 File Offset: 0x00000A78
		protected override void Dispose(bool disposing)
		{
			bool flag = disposing && this.components != null;
			if (flag)
			{
				this.components.Dispose();
			}
			base.Dispose(disposing);
		}

		// Token: 0x06000024 RID: 36 RVA: 0x000028B0 File Offset: 0x00000AB0
		private void InitializeComponent()
		{
			this.label1 = new global::System.Windows.Forms.Label();
			this.label2 = new global::System.Windows.Forms.Label();
			this.label3 = new global::System.Windows.Forms.Label();
			base.SuspendLayout();
			this.label1.AutoSize = true;
			this.label1.Font = new global::System.Drawing.Font("Microsoft Sans Serif", 11.25f, global::System.Drawing.FontStyle.Regular, global::System.Drawing.GraphicsUnit.Point, 0);
			this.label1.Location = new global::System.Drawing.Point(2, 1);
			this.label1.Name = "label1";
			this.label1.Size = new global::System.Drawing.Size(24, 18);
			this.label1.TabIndex = 0;
			this.label1.Text = "11";
			this.label2.AutoSize = true;
			this.label2.Font = new global::System.Drawing.Font("Microsoft Sans Serif", 11.25f, global::System.Drawing.FontStyle.Regular, global::System.Drawing.GraphicsUnit.Point, 0);
			this.label2.Location = new global::System.Drawing.Point(32, 1);
			this.label2.Name = "label2";
			this.label2.Size = new global::System.Drawing.Size(24, 18);
			this.label2.TabIndex = 1;
			this.label2.Text = "11";
			this.label3.AutoSize = true;
			this.label3.Font = new global::System.Drawing.Font("Microsoft Sans Serif", 11.25f, global::System.Drawing.FontStyle.Regular, global::System.Drawing.GraphicsUnit.Point, 0);
			this.label3.Location = new global::System.Drawing.Point(61, 1);
			this.label3.Name = "label3";
			this.label3.Size = new global::System.Drawing.Size(24, 18);
			this.label3.TabIndex = 2;
			this.label3.Text = "11";
			base.AutoScaleDimensions = new global::System.Drawing.SizeF(6f, 13f);
			base.AutoScaleMode = global::System.Windows.Forms.AutoScaleMode.Font;
			this.AutoValidate = global::System.Windows.Forms.AutoValidate.Disable;
			this.BackColor = global::System.Drawing.Color.Red;
			this.BackgroundImageLayout = global::System.Windows.Forms.ImageLayout.None;
			base.ClientSize = new global::System.Drawing.Size(88, 21);
			base.ControlBox = false;
			base.Controls.Add(this.label3);
			base.Controls.Add(this.label2);
			base.Controls.Add(this.label1);
			base.FormBorderStyle = global::System.Windows.Forms.FormBorderStyle.None;
			base.Name = "Form2";
			this.RightToLeft = global::System.Windows.Forms.RightToLeft.No;
			base.ShowInTaskbar = false;
			this.Text = "Form2";
			base.Load += new global::System.EventHandler(this.Form2_Load);
			base.ResumeLayout(false);
			base.PerformLayout();
		}

		// Token: 0x0400001C RID: 28
		private global::System.ComponentModel.IContainer components = null;

		// Token: 0x0400001D RID: 29
		private global::System.Windows.Forms.Label label1;

		// Token: 0x0400001E RID: 30
		private global::System.Windows.Forms.Label label2;

		// Token: 0x0400001F RID: 31
		private global::System.Windows.Forms.Label label3;
	}
}
