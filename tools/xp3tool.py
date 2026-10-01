#!/usr/bin/env python3
"""xp3tool.py - List and extract files from KiriKiri XP3 archives."""
import struct, zlib, sys, os

def read_xp3(filename):
    with open(filename, 'rb') as f:
        # Header: 11-byte mark + 8-byte index offset
        mark = f.read(11)
        if mark[:3] != b'XP3':
            print(f"Not an XP3 archive: {filename}", file=sys.stderr)
            return []
        index_offset = struct.unpack('<Q', f.read(8))[0]
        
        # Seek to index
        f.seek(index_offset)
        first_byte = f.read(1)[0]
        
        if first_byte == 0x01:
            # Compressed index
            comp_size = struct.unpack('<Q', f.read(8))[0]
            uncomp_size = struct.unpack('<Q', f.read(8))[0]
            comp_data = f.read(comp_size)
            data = zlib.decompress(comp_data)
        else:
            # Uncompressed - seek back and read raw
            f.seek(index_offset)
            data = f.read()
        
        return parse_index(data, f, filename)

def parse_index(data, f, filename):
    files = []
    pos = 0
    while pos + 12 <= len(data):
        chunk_type = data[pos:pos+4]
        chunk_size = struct.unpack_from('<Q', data, pos+4)[0]
        pos += 12
        
        if chunk_type == b'file':
            file_info = parse_file_chunk(data, pos, chunk_size, f, filename)
            if file_info:
                files.extend(file_info)
        
        pos += chunk_size
    return files

def parse_file_chunk(data, start, size, f, filename):
    files = []
    pos = start
    end = start + size
    
    name = None
    segments = []
    
    while pos + 12 <= end and pos + 12 <= len(data):
        sub_type = data[pos:pos+4]
        sub_size = struct.unpack_from('<Q', data, pos+4)[0]
        pos += 12
        
        if sub_type == b'info':
            flags = struct.unpack_from('<I', data, pos)[0]
            orgsize = struct.unpack_from('<Q', data, pos+4)[0]
            arcsize = struct.unpack_from('<Q', data, pos+12)[0]
            namelen = struct.unpack_from('<H', data, pos+20)[0]
            name = data[pos+22:pos+22+namelen*2].decode('utf-16-le', errors='replace')
        
        elif sub_type == b'segm':
            seg_count = sub_size // 28
            for i in range(seg_count):
                base = pos + i * 28
                seg_flags = struct.unpack_from('<I', data, base)[0]
                offset = struct.unpack_from('<Q', data, base+4)[0]
                org_size = struct.unpack_from('<Q', data, base+12)[0]
                arc_size = struct.unpack_from('<Q', data, base+20)[0]
                is_compressed = (seg_flags & 0x07) == 1
                segments.append({
                    'offset': offset,
                    'org_size': org_size,
                    'arc_size': arc_size,
                    'compressed': is_compressed
                })
        
        pos += sub_size
    
    if name:
        files.append({'name': name, 'segments': segments, 'org_size': orgsize})
    return files

def extract_file(file_info, xp3_filename, output_dir):
    name = file_info['name']
    safe_name = name.replace('\\', '/')
    out_path = os.path.join(output_dir, safe_name)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    
    with open(xp3_filename, 'rb') as f:
        data = b''
        for seg in file_info['segments']:
            f.seek(seg['offset'])
            seg_data = f.read(seg['arc_size'])
            if seg['compressed']:
                try:
                    seg_data = zlib.decompress(seg_data)
                except:
                    pass
            data += seg_data
    
    with open(out_path, 'wb') as f:
        f.write(data)
    print(f"  Extracted: {name} ({len(data)} bytes)")

def main():
    if len(sys.argv) < 2:
        print("Usage: xp3tool.py <file.xp3> [extract <output_dir>]", file=sys.stderr)
        sys.exit(1)
    
    xp3_file = sys.argv[1]
    files = read_xp3(xp3_file)
    
    if len(sys.argv) >= 4 and sys.argv[2] == 'extract':
        output_dir = sys.argv[3]
        for f in files:
            extract_file(f, xp3_file, output_dir)
    else:
        print(f"XP3 archive: {xp3_file}")
        print(f"Files: {len(files)}")
        for f in files:
            print(f"  {f['name']} ({f.get('org_size', '?')} bytes)")

if __name__ == '__main__':
    main()
