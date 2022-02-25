
import argparse
import ctypes
import hashlib
import os
import pathlib
import sys
import xml.etree.ElementTree as ET

LIB = ctypes.cdll.LoadLibrary('./fb_decode.so')

class Asset(object):
	def __init__(self, name, offset, size):
		self.name   = name
		self.offset = int(offset, 16)
		self.size   = int(size)
	def read(self, rom):
		return rom[self.offset:self.offset + self.size]
	def dump(self, rom):
		with open(self.name, 'wb') as f:
			f.write(self.read(rom))

def decode(rom, node, dumpfiles):
	files = node.find('files').findall('file')
	print('Found %d files' % len(files))
	assets = {}
	for f in files:
		name = f.get('name')
		assets[name] = Asset(name, f.get('offset'), f.get('size'))
	for filename, asset in assets.items():
		if dumpfiles:
			asset.dump(rom)
		name, ext = filename.split('.', 1)
		if ext == 'LEV':
			lev = asset.read(rom)
			mbk = assets.get(name + '.MBK').read(rom)
			pal = assets.get(name + '.PAL').read(rom)
			sgd = assets.get(name + '.SGD').read(rom) if name == 'LEVEL1' else None
			LIB.decodeLEV(bytes(asset.name, 'ascii'), lev, mbk, pal, sgd)
		elif ext == 'RP':
			rp  = asset.read(rom)
			spc = assets.get('GLOBAL.SPC').read(rom)
			mbk = assets.get('SPC.MBK').read(rom)
			LIB.decodeRP(bytes(asset.name, 'ascii'), rp, spc, mbk)
		elif filename == 'GLOBAL.SPC':
			spc = asset.read(rom)
			mbk = assets.get('SPC.MBK').read(rom)
			LIB.decodeSPC(bytes(asset.name, 'ascii'), spc, mbk)
		elif filename == 'GLOBAL.SPR':
			spr = asset.read(rom)
			tab = assets.get('GLOBAL.TAB').read(rom)
			LIB.decodeSPR(bytes(asset.name, 'ascii'), spr, tab)
		else:
			dat = asset.read(rom)
			LIB.decode(bytes(asset.name, 'ascii'), dat, len(dat))

if __name__ == '__main__':
	parser = argparse.ArgumentParser(description='Flashback genesis extraction tool')
	parser.add_argument('--dump', action='store_true')
	parser.add_argument('--output_dir')
	parser.add_argument('rom')
	args = parser.parse_args()
	with open(args.rom, 'rb') as f:
		rom = f.read()
		sha1 = hashlib.sha1(rom).hexdigest()
		root = ET.parse('roms.xml').getroot()
		for node in root.findall('rom'):
			h = node.find('hash').get('sha1')
			if h == sha1:
				print('Found matching ROM')
				if args.output_dir:
					os.chdir(args.output_dir)
				decode(rom, node, args.dump)
				break
