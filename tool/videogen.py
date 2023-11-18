#! /usr/bin/env python3
'''
Generate audio video synchronization video.
'''

import argparse
import math
import os
import shutil
import subprocess
from PIL import Image
import qrcode


def _div_ceil(n, m):
    return (n + m - 1) // m


class Context:
    '''
    Holds context information
    Field:
    - workdir -- Working directory
    - vr -- Video framerate
    - ar -- Audio sampling frequency
    - width -- Video width
    - height -- Video height
    '''
    def __init__(self, workdir, vr, ar):
        '''
        Initializer
        Argument:
        - workdir -- Set the working directory
        '''
        self.workdir = os.path.abspath(workdir)
        try:
            vr = int(vr)
            self.vr = (vr, 1)
        except ValueError:
            vr = vr.split('/', 1)
            self.vr = (int(vr[0]), int(vr[1]))
        self.ar = int(ar)
        self.width = 1280
        self.height = 720
        self._img_index = 0
        self._sync_image_cache = {}

    def next_image(self):
        '''
        Returns a file name for the next temporarily image file.
        '''
        self._img_index += 1
        return f'{self.workdir}/image-{self._img_index:04d}.png'

    def sync_image(self, ix):
        '''
        Returns a file name for the synchrnization image.
        '''
        if ix in self._sync_image_cache:
            return self._sync_image_cache[ix]

        base_img = Image.new('RGB', (self.width, self.height), (0, 0, 0))
        w, h = self.width // 2, self.height // 2
        white = Image.new('RGB', (w, h), (255, 255, 255))
        if ix == 0:
            base_img.paste(white, (0, 0))
            base_img.paste(white, (w, h))
        elif ix == 1:
            base_img.paste(white, (w, 0))
            base_img.paste(white, (0, h))
        name = self.next_image()
        base_img.save(name)
        self._sync_image_cache[ix] = name
        return name


class Pattern:
    '''
    Store synchronization pattern information
    '''
    def __init__(self, ctx, settings):
        self.ctx = ctx
        self._prepare(settings)
        self.video_cache = None
        self.audio_cache = None

    def _prepare(self, settings):
        q = 2
        t = 2
        f = 442
        c = 0
        for s in settings.split(','):
            k, v = s.split('=', 1)
            if k == 'q':
                q = int(v)
            elif k == 't':
                t = int(v)
            elif k == 'f':
                f = int(v)
            elif k == 'c':
                c = int(v)
            else:
                raise ValueError(f'Invalid keyword {k} in {settings}')
        if c <= 0:
            c = t * f * self.ctx.vr[1] // self.ctx.vr[0]
        # Needs t / vr >= c / f
        if c <= 0:
            min_f = _div_ceil(self.ctx.vr[0], t * self.ctx.vr[1])
            raise ValueError(f'{settings}: Audio frequency is too slow, required at least {min_f}')
        if t * f * self.ctx.vr[1] < c * self.ctx.vr[0]:
            audio_duration = c / f
            video_duration = t * self.ctx.vr[1] / self.ctx.vr[0]
            raise ValueError('Too short video; '
                             + f'video marker duration {video_duration} second, '
                             + f'audio marker duration {audio_duration} second')
        self.q, self.t, self.f, self.c = q, t, f, c

    def _gen_qrcode(self):
        ctx = self.ctx
        f, c = self.f, self.c
        q_ms = self.q * 1000 * ctx.vr[1] // ctx.vr[0]
        t_ms = self.q * 1000 * ctx.vr[1] // ctx.vr[0]
        qr_img = qrcode.make(f'q={q_ms},t={t_ms},f={f},c={c}',
                             error_correction=qrcode.constants.ERROR_CORRECT_M).get_image()
        size = min(ctx.width, ctx.height)
        qr_img = qr_img.resize((size, size))
        base_img = Image.new('RGB', (ctx.width, ctx.height), (127, 127, 127))
        offset = ((ctx.width - size) // 2, (ctx.height - size) // 2)
        base_img.paste(qr_img, offset)
        qr_filename = ctx.next_image()
        base_img.save(qr_filename)
        return qr_filename

    def video_frames(self):
        '''
        Returns a list of video frame files
        '''
        if self.video_cache:
            return self.video_cache
        qr_filename = self._gen_qrcode()
        r_qr = [qr_filename] * self.q
        r_s0 = [self.ctx.sync_image(0)] * self.t
        r_s1 = [self.ctx.sync_image(1)] * self.t
        self.video_cache =  r_qr + r_s0 + r_s1
        return self.video_cache

    def audio_frames(self):
        '''
        Returns a audio frame bytes
        '''
        if self.audio_cache:
            return self.audio_cache
        q, t, f, c = self.q, self.t, self.f, self.c
        ctx = self.ctx
        n_center = ctx.ar * (q + t) * ctx.vr[1] // ctx.vr[0]
        n_all = ctx.ar * (q + t * 2) * ctx.vr[1] // ctx.vr[0]
        i = 0
        buffer = b''
        while i < n_all:
            phase = abs(i - n_center)
            if phase * f < c * ctx.ar:
                phase = phase % ctx.ar
                sample = math.sin(phase * 2 * math.pi * f / ctx.ar)
                sample = round(sample * 32767)
            else:
                sample = 0
            buffer += (sample & 0xFFFF).to_bytes(2, 'little')
            i += 1
        self.audio_cache = buffer
        return buffer


def _simplify_fraction(f):
    n = math.gcd(f[0], f[1])
    return (f[0] // n, f[1] // n)


def _calculate_duration(v_num, v_den, ar):
    ar_num = ar * v_den
    n = math.gcd(v_num, ar_num)
    return v_num * ar_num // n


class VideoGen:
    '''
    Generate the video file
    '''
    def __init__(self, ctx):
        self.ctx = ctx
        self.patterns = []

    def prepare(self):
        '''
        Generates pattern data
        '''

    def _generate_video(self, i0_fmt, n_repeat):
        i = 0
        i_frame = 0
        while i < n_repeat:
            for p in self.patterns:
                for f in p.video_frames():
                    os.link(f, i0_fmt % i_frame)
                    i_frame += 1
            i += 1

    def _generate_audio(self, i1_name, n_repeat):
        ctx = self.ctx
        with open(i1_name, 'wb') as i1:
            i = 0
            i_audio_frame = 0
            i_video_frame = 0
            while i < n_repeat:
                for p in self.patterns:
                    buf = p.audio_frames()
                    i1.write(buf)
                    i_audio_frame += len(buf) // 2
                    i_video_frame += len(p.video_frames())
                    # i_audio_frame / ctx.ar < i_video_frame * ctx.vr[1] / ctx.vr[0]
                    if i_audio_frame * ctx.vr[0] < i_video_frame * ctx.vr[1] * ctx.ar:
                        i1.write(int(0).to_bytes(2, 'little'))
                        i_audio_frame += 1
                i += 1

    def output(self, filename, duration=0, max_duration=0):
        '''
        Outputs to file
        '''
        ctx = self.ctx
        video_frames = []
        for p in self.patterns:
            video_frames += p.video_frames()
        video_unit_sec = (len(video_frames) * ctx.vr[1], ctx.vr[0])
        video_unit_sec = _simplify_fraction(video_unit_sec)

        if not duration:
            duration = _calculate_duration(video_unit_sec[0], video_unit_sec[1], ctx.ar)
        n_repeat = _div_ceil(duration * video_unit_sec[1], video_unit_sec[0])
        if n_repeat * video_unit_sec[0] > max_duration * video_unit_sec[1]:
            n_repeat = max_duration * video_unit_sec[1] // video_unit_sec[0]
        duration = n_repeat * video_unit_sec[0] / video_unit_sec[1]

        print(f'Info: generating {duration} seconds video with {n_repeat} loops')

        i0_dir = ctx.workdir + '/i0'
        i0_fmt = i0_dir + '/frame-%06d.png'
        shutil.rmtree(i0_dir, ignore_errors=True)
        os.mkdir(i0_dir)
        self._generate_video(i0_fmt, n_repeat)

        i1_name = ctx.workdir + '/i1.pcm'
        self._generate_audio(i1_name, n_repeat)

        ffmpeg_cmd = ['ffmpeg']
        ffmpeg_cmd += ['-hide_banner']
        if ctx.vr[1] == 1:
            ffmpeg_cmd += ['-framerate', '%d'%ctx.vr[0]]
        else:
            ffmpeg_cmd += ['-framerate', '%d/%d'%ctx.vr]
        ffmpeg_cmd += ['-i', i0_fmt]
        ffmpeg_cmd += ['-f', 's16le', '-ac', '1', '-ar', '%d'%ctx.ar, '-i', i1_name]
        ffmpeg_cmd += ['-pix_fmt', 'yuv420p']
        ffmpeg_cmd += ['-y', filename]
        subprocess.run(ffmpeg_cmd, check=True)
        os.unlink(i1_name)
        shutil.rmtree(i0_dir)


_help_patterns = '''
patterns:
  The pattern specifier is a comma (,) separated list of parameters.
  Each parameter consist of keys listed below and '=' and its value.
  - q -- Number of video frames for the QR code
  - t -- Number of video frames for the synchronization marker
  - f -- Frequency of the audio synchronization marker
  - c -- Number of cycles of the audio synchronization marker'''

def _main():
    parser = argparse.ArgumentParser(
            description='Generate audio video synchronization video',
            epilog=_help_patterns,
            formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument('-w', '--workdir', action='store', default='.',
                        help='Store temporarily files under this directory')
    parser.add_argument('--vr', action='store', default='30', help='Video framerate')
    parser.add_argument('--ar', action='store', default='48000', help='Audio sampling frequency')
    parser.add_argument('--dryrun', action='store_true', default=False,
                        help='Do not actually generate the video file')
    parser.add_argument('--duration', action='store', default='0',
                        help='Duration of the video, 0 for auto-calculation')
    parser.add_argument('--max-duration', action='store', default='600', help='Maximum duration')
    parser.add_argument('-o', '--output', action='store', default='output.mp4',
                        help='Output file name')
    parser.add_argument('patterns', nargs='+', help='Pattern definition of synchronization marker')
    args = parser.parse_args()
    ctx = Context(args.workdir, args.vr, args.ar)
    gen = VideoGen(ctx)
    for p in args.patterns:
        p = Pattern(ctx, p)
        gen.patterns.append(p)
    gen.prepare()
    if not args.dryrun:
        gen.output(args.output, duration=int(args.duration), max_duration=int(args.max_duration))


if __name__ == '__main__':
    _main()
